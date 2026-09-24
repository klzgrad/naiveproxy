// Copyright 2026 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "quiche/quic/moqt/moqt_uni_stream.h"

#include <cstdint>
#include <memory>
#include <optional>
#include <utility>
#include <vector>

#include "absl/base/casts.h"
#include "absl/base/nullability.h"
#include "absl/memory/memory.h"
#include "absl/status/status.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"
#include "absl/types/span.h"
#include "quiche/quic/core/quic_time.h"
#include "quiche/quic/core/quic_utils.h"
#include "quiche/quic/moqt/moqt_error.h"
#include "quiche/quic/moqt/moqt_fetch_task.h"
#include "quiche/quic/moqt/moqt_framer.h"
#include "quiche/quic/moqt/moqt_messages.h"
#include "quiche/quic/moqt/moqt_object.h"
#include "quiche/quic/moqt/moqt_object_subscriber.h"
#include "quiche/quic/moqt/moqt_priority.h"
#include "quiche/quic/moqt/moqt_publisher.h"
#include "quiche/quic/moqt/moqt_trace_recorder.h"
#include "quiche/quic/moqt/moqt_types.h"
#include "quiche/common/platform/api/quiche_bug_tracker.h"
#include "quiche/common/quiche_buffer_allocator.h"
#include "quiche/common/quiche_mem_slice.h"
#include "quiche/common/quiche_weak_ptr.h"
#include "quiche/web_transport/stream_helpers.h"
#include "quiche/web_transport/web_transport.h"

namespace moqt {

void OutgoingUniStream::UpdatePriority(MoqtPriority subscriber_priority) {
  priority_.send_order = UpdateSendOrderForSubscriberPriority(
      priority_.send_order, subscriber_priority);
  stream_.SetPriority(priority_);
}

bool OutgoingUniStream::WriteObjectToStream(PublishedObject& object,
                                            MoqtDataStreamType type) {
  MoqtObject header;
  header.track_alias = track_identifier_;
  header.group_id = object.metadata.location.group;
  header.subgroup_id = object.metadata.subgroup;
  header.object_id = object.metadata.location.object;
  header.publisher_priority = object.metadata.publisher_priority;
  header.extension_headers = object.metadata.extensions;
  header.object_status = object.metadata.status;
  header.payload_length = object.metadata.payload_length;

  quiche::QuicheBuffer serialized_header =
      framer_.SerializeObjectHeader(header, type, last_object_);
  std::vector<quiche::QuicheMemSlice> write_vector;
  write_vector.reserve(object.payload.size() + 1);
  write_vector.push_back(quiche::QuicheMemSlice(std::move(serialized_header)));
  for (auto& slice : object.payload) {
    write_vector.push_back(std::move(slice));
  }
  webtransport::StreamWriteOptions options;
  options.set_send_fin(!type.IsFetch() && object.fin_after_this);
  absl::Status write_status =
      stream_.Writev(absl::MakeSpan(write_vector), options);
  if (!write_status.ok()) {
    QUICHE_BUG(MoqtSession_WriteObjectToStream_write_failed)
        << "Writing into MoQT stream failed despite CanWrite being true "
           "before; status: "
        << write_status;
    return false;
  }
  QUIC_DVLOG(1) << "Stream " << stream_.GetStreamId() << " successfully wrote "
                << object.metadata.location
                << ", fin = " << object.fin_after_this;
  return true;
}

OutgoingSubgroupStream::OutgoingSubgroupStream(
    MoqtFramer framer, webtransport::Stream* absl_nonnull stream,
    DataStreamIndex index, uint64_t first_object,
    quiche::QuicheWeakPtr<LivePublisherInterface> visitor,
    std::shared_ptr<MoqtTrackPublisher> absl_nonnull track_publisher,
    webtransport::StreamPriority priority, uint64_t track_alias,
    MoqtTraceRecorder* absl_nonnull trace_recorder)
    : OutgoingUniStream(framer, stream, priority, track_alias),
      index_(index),
      visitor_(std::move(visitor)),

      track_alias_(track_alias),
      publisher_(track_publisher),
      next_object_(first_object) {
  trace_recorder->RecordSubgroupStreamCreated(stream->GetStreamId(),
                                              track_alias_, index);
}

OutgoingSubgroupStream::~OutgoingSubgroupStream() {
  // Though it might seem intuitive that the session object has to outlive the
  // connection object (and this is indeed how something like QuicSession and
  // QuicStream works), this is not the true for WebTransport visitors: the
  // session getting destroyed will inevitably lead to all related streams being
  // destroyed, but the actual order of destruction is not guaranteed.  Thus, we
  // need to check if the session still exists while accessing it in a stream
  // destructor.
  if (delivery_timeout_alarm_ != nullptr) {
    delivery_timeout_alarm_->PermanentCancel();
  }
  LivePublisherInterface* visitor = visitor_.GetIfAvailable();
  if (visitor != nullptr) {
    visitor->OnDataStreamDestroyed(index_);
  }
}

void OutgoingSubgroupStream::OnCanWrite() { SendObjects(); }

void OutgoingSubgroupStream::OnStopSendingReceived(
    webtransport::StreamErrorCode error_code) {
  LivePublisherInterface* visitor = visitor_.GetIfAvailable();
  if (visitor != nullptr) {
    visitor->OnSubgroupAbandoned(index_.group, index_.subgroup, error_code);
  }
}

void OutgoingSubgroupStream::DeliveryTimeoutDelegate::OnAlarm() {
  LivePublisherInterface* visitor = stream_->visitor_.GetIfAvailable();
  if (visitor != nullptr) {
    visitor->OnStreamTimeout(stream_->index_);
  }
  stream_->stream().ResetWithUserCode(kResetCodeDeliveryTimeout);
}

void OutgoingSubgroupStream::SendObjects() {
  LivePublisherInterface* visitor = visitor_.GetIfAvailable();
  if (visitor == nullptr) {
    return;
  }
  while (stream().CanWrite()) {
    std::optional<PublishedObject> object = publisher_->GetCachedObject(
        index_.group, index_.subgroup, next_object_, already_delivered_);
    if (!object.has_value()) {
      break;
    }
    if (object->metadata.payload_length > 0 && object->payload.empty()) {
      QUICHE_BUG(OutgoingSubgroupStream_empty_payload)
          << "Received non-empty object with no payload";
      return;
    }
    QUICHE_BUG_IF(OutgoingSubgroupStream_SendObjects_no_first_object,
                  !object->metadata.first_object_in_subgroup.has_value())
        << "first_object_in_subgroup has to be set on all objects set via "
           "subscription";
    QUICHE_DCHECK_EQ(object->metadata.location.group, index_.group);
    QUICHE_DCHECK(object->metadata.subgroup == index_.subgroup);
    if (!visitor->InWindow(object->metadata.location)) {
      // It is possible that the next object became irrelevant due to a
      // REQUEST_UPDATE.  Close the stream if so.
      absl::Status status = webtransport::SendFinOnStream(stream());
      QUICHE_BUG_IF(OutgoingSubgroupStream_fin_due_to_update, !status.ok())
          << "Writing FIN failed despite CanWrite() being true.";
      return;
    }

    quic::QuicTimeDelta delivery_timeout = visitor->delivery_timeout();
    if (!visitor->alternate_delivery_timeout() &&
        visitor->clock()->ApproximateNow() - object->metadata.arrival_time >
            delivery_timeout) {
      visitor->OnStreamTimeout(index_);
      stream().ResetWithUserCode(kResetCodeDeliveryTimeout);
      // No class access below this line.
      return;
    }
    // Always include extension header length, because it's difficult to know
    // a priori if they're going to appear on a stream.
    if (!last_object().has_value()) {
      type_ = MoqtDataStreamType::Subgroup(
          index_.subgroup, next_object_, false,
          object->metadata.publisher_priority ==
              publisher_->extensions().default_publisher_priority(),
          object->metadata.first_object_in_subgroup.value_or(true));
    }
    uint64_t start_offset = already_delivered_;
    already_delivered_ +=
        quic::MemSliceSpanTotalSize(absl::MakeSpan(object->payload));
    object->fin_after_this &=
        already_delivered_ == object->metadata.payload_length;
    if (start_offset > 0) {  // Just send payload.
      if (already_delivered_ == start_offset) {
        // Partial delivery of an object but the payload is empty. This would
        // result in an infinite loop.
        QUICHE_BUG(OutgoingDataStream_empty_payload)
            << "Empty payload for partial object " << object->metadata.location;
        return;
      }
      webtransport::StreamWriteOptions options;
      options.set_send_fin(object->fin_after_this);
      absl::Status write_status =
          stream().Writev(absl::MakeSpan(object->payload), options);
      if (!write_status.ok()) {
        QUICHE_BUG(MoqtSession_WriteObjectToStream_write_failed)
            << "Writing into MoQT stream failed despite CanWrite() being true "
               "before; status: "
            << write_status;
        stream().ResetWithUserCode(kResetCodeInternalError);
        return;
      }
    } else {
      if (!WriteObjectToStream(*object, type_)) {
        stream().ResetWithUserCode(kResetCodeInternalError);
        // No class access below this line.
        return;
      }
      set_last_object(object->metadata);
      next_object_ = object->metadata.location.object;
      visitor->OnObjectSent(object->metadata.location);
    }
    QUICHE_DCHECK(last_object().has_value());
    if (already_delivered_ != last_object()->payload_length) {
      return;
    }
    ++next_object_;
    already_delivered_ = 0;
    if (object->fin_after_this && !delivery_timeout.IsInfinite() &&
        !visitor->alternate_delivery_timeout()) {
      CreateAndSetAlarm(object->metadata.arrival_time + delivery_timeout);
    }
  }
}

void OutgoingSubgroupStream::Fin(Location last_object) {
  QUICHE_DCHECK_EQ(last_object.group, index_.group);
  if (next_object_ <= last_object.object) {
    // There is still data to send, do nothing.
    return;
  }
  // All data has already been sent; send a pure FIN.
  absl::Status status = webtransport::SendFinOnStream(stream());
  QUICHE_BUG_IF(OutgoingSubgroupStream_fin_failed, !status.ok())
      << "Writing pure FIN failed.";
  LivePublisherInterface* visitor = visitor_.GetIfAvailable();
  if (visitor == nullptr) {
    return;
  }
  quic::QuicTimeDelta delivery_timeout = visitor->delivery_timeout();
  if (!delivery_timeout.IsInfinite()) {
    CreateAndSetAlarm(visitor->clock()->ApproximateNow() + delivery_timeout);
  }
}

void OutgoingSubgroupStream::CreateAndSetAlarm(quic::QuicTime deadline) {
  if (delivery_timeout_alarm_ != nullptr) {
    return;
  }
  LivePublisherInterface* visitor = visitor_.GetIfAvailable();
  if (visitor == nullptr) {
    return;
  }
  delivery_timeout_alarm_ = absl::WrapUnique(
      visitor->alarm_factory()->CreateAlarm(new DeliveryTimeoutDelegate(this)));
  delivery_timeout_alarm_->Set(deadline);
}

OutgoingFetchStream::OutgoingFetchStream(
    MoqtFramer framer, webtransport::Stream* absl_nonnull stream,
    uint64_t request_id, webtransport::StreamPriority priority,
    std::unique_ptr<MoqtFetchTask> incoming_objects,
    FetchStreamCloseCallback close_callback,
    MoqtTraceRecorder* absl_nonnull trace_recorder)
    : OutgoingUniStream(framer, stream, priority, request_id),
      incoming_objects_(std::move(incoming_objects)),
      close_callback_(std::move(close_callback)) {
  incoming_objects_->SetObjectAvailableCallback(
      [this]() { this->OnCanWrite(); });
  trace_recorder->RecordFetchStreamCreated(stream->GetStreamId());
}

OutgoingFetchStream::~OutgoingFetchStream() {
  if (close_callback_ != nullptr) {
    std::move(close_callback_)();
  }
  close_callback_ = nullptr;
}

void OutgoingFetchStream::OnCanWrite() {
  PublishedObject object;
  while (stream().CanWrite()) {
    MoqtFetchTask::GetNextObjectResult result =
        incoming_objects_->GetNextObject(object);
    switch (result) {
      case MoqtFetchTask::GetNextObjectResult::kSuccess:
        // Skip ObjectDoesNotExist in FETCH.
        if (object.metadata.status != MoqtObjectStatus::kNormal) {
          QUICHE_BUG(quiche_bug_got_doesnotexist_in_fetch)
              << "Got Non-normal object in FETCH";
          continue;
        }
        if (last_object().has_value() &&
            object.metadata.location == last_object()->location) {
          // This is the continuation of the previous object.
          webtransport::StreamWriteOptions options;
          absl::Status write_status =
              stream().Writev(absl::MakeSpan(object.payload), options);
          if (!write_status.ok()) {
            QUICHE_BUG(MoqtSession_WriteObjectToStream_write_failed)
                << "Writing into MoQT stream failed despite CanWrite() being "
                   "true before; status: "
                << write_status;
            stream().ResetWithUserCode(kResetCodeInternalError);
            return;
          }
          break;
        }
        if (WriteObjectToStream(object, MoqtDataStreamType::Fetch())) {
          set_last_object(object.metadata);
        }
        break;
      case MoqtFetchTask::GetNextObjectResult::kPending:
        return;
      case MoqtFetchTask::GetNextObjectResult::kEof:
        // TODO(martinduke): Either prefetch the next object, or alter the API
        // so that we're not sending FIN in a separate frame.
        if (!webtransport::SendFinOnStream(stream()).ok()) {
          QUICHE_DVLOG(1) << "Sending FIN onStream " << stream().GetStreamId()
                          << " failed";
        }
        return;
      case MoqtFetchTask::GetNextObjectResult::kError:
        stream().ResetWithUserCode(static_cast<webtransport::StreamErrorCode>(
            incoming_objects_->GetStatus().code()));
        return;
    }
  }
}

void OutgoingFetchStream::OnStopSendingReceived(
    webtransport::StreamErrorCode error_code) {
  stream().ResetWithUserCode(error_code);
}

IncomingDataStream::~IncomingDataStream() {
  QUICHE_DVLOG(1) << "Destroying incoming data stream "
                  << stream_->GetStreamId();
  if (!parser_.track_alias().has_value()) {
    QUIC_DVLOG(1) << "Destroying incoming data stream before "
                     "learning track alias";
    return;
  }
  if (!track_.IsValid()) {
    return;
  }
  if (IsFetch()) {
    auto fetch = absl::down_cast<UpstreamFetch*>(track_.GetIfAvailable());
    if (fetch != nullptr) {
      fetch->OnStreamClosed();
    }
    return;
  }
  // It's a subscribe.
  auto subscribe = absl::down_cast<LiveSubscriber*>(track_.GetIfAvailable());
  if (subscribe == nullptr) {
    return;
  }
  subscribe->OnStreamClosed(fin_received_, index_);
}

void IncomingDataStream::OnObjectMessage(const MoqtObject& message,
                                         absl::string_view payload,
                                         bool end_of_message) {
  QUICHE_DVLOG(1) << "Received OBJECT message on stream "
                  << stream_->GetStreamId() << " for track alias "
                  << message.track_alias << " with sequence "
                  << message.group_id << ":" << message.object_id
                  << " priority " << message.publisher_priority << " length "
                  << payload.size() << " length " << message.payload_length
                  << (end_of_message ? "F" : "");
  if (!session_->deliver_partial_objects()) {
    if (!end_of_message) {  // Buffer partial object.
      if (partial_object_.empty()) {
        // Avoid redundant allocations by reserving the appropriate amount of
        // memory if known.
        partial_object_.reserve(message.payload_length);
      }
      absl::StrAppend(&partial_object_, payload);
      return;
    }
    if (!partial_object_.empty()) {  // Completes the object
      absl::StrAppend(&partial_object_, payload);
      payload = absl::string_view(partial_object_);
    }
  }
  if (payload.empty() && bytes_received_this_object_ > 0 && !end_of_message) {
    return;  // Nothing arrived.
  }
  if (!parser_.track_alias().has_value()) {
    QUICHE_BUG(quic_bug_object_with_no_stream_type)
        << "Object delivered without preliminaries";
    return;
  }
  // Get a pointer to the upstream state.
  if (!track_.IsValid()) {
    track_ = IsFetch() ? session_->GetFetch(message.track_alias)
                       : session_->GetSubscribe(message.track_alias);
  }
  if (!track_.IsValid()) {
    // The request has gone away.
    stream_->SendStopSending(kResetCodeCancelled);
    return;
  }
  Location location(message.group_id, message.object_id);
  ObjectSubscriber* track = track_.GetIfAvailable();
  if (track == nullptr ||
      !track->InWindow(Location(message.group_id, message.object_id))) {
    // This is not an error. It can be the result of a recent REQUEST_UPDATE or
    // UNSUBSCRIBE.
    return;
  }
  if (!IsFetch()) {
    if (!index_.has_value()) {
      if (!message.subgroup_id.has_value()) {
        QUICHE_BUG(quiche_bug_moqt_subgroup_id_missing)
            << "Missing subgroup ID on SUBSCRIBE stream";
        return;
      }
      index_ = DataStreamIndex(message.group_id, *message.subgroup_id);
    }
    if (no_more_objects_) {
      // Already got a stream-ending object. While the lower layer won't
      // deliver data after the FIN, there could have been an EndOfGroup or
      // EndOfTrack signal.
      session_->OnMalformedTrack(track);
      return;
    }
    if (end_of_message) {
      next_object_id_ = message.object_id + 1;
      if (message.object_status == MoqtObjectStatus::kEndOfTrack ||
          message.object_status == MoqtObjectStatus::kEndOfGroup) {
        no_more_objects_ = true;
      }
    }
    LiveSubscriber* subscribe = absl::down_cast<LiveSubscriber*>(track);
    subscribe->OnObjectOrOk();
    if (visitor_ != nullptr) {
      PublishedObjectMetadata metadata;
      metadata.location = Location(message.group_id, message.object_id);
      metadata.subgroup = message.subgroup_id;
      metadata.extensions = message.extension_headers;
      metadata.status = message.object_status;
      metadata.publisher_priority = message.publisher_priority;
      metadata.first_object_in_subgroup = message.first_object_in_subgroup;
      metadata.payload_length = message.payload_length;
      metadata.arrival_time = clock_->Now();
      visitor_->OnObjectFragment(track->full_track_name(), metadata, payload,
                                 bytes_received_this_object_);
    }
  } else {  // FETCH
    track->OnObjectOrOk();
    UpstreamFetch* fetch = absl::down_cast<UpstreamFetch*>(track);
    UpstreamFetch::UpstreamFetchTask* task = fetch->task();
    if (task == nullptr) {
      // The application killed the FETCH.
      stream_->SendStopSending(kResetCodeCancelled);
      return;
    }
    if (!task->HasObject()) {
      task->NewObject(message);
    }
    if (task->NeedsMorePayload() && !payload.empty()) {
      task->AppendPayloadToObject(payload);
    }
  }
  if (end_of_message) {
    bytes_received_this_object_ = 0;
  } else {
    bytes_received_this_object_ += payload.size();
  }
  partial_object_.clear();
}

void IncomingDataStream::MaybeReadOneObject() {
  if (!parser_.track_alias().has_value() ||
      !parser_.stream_type().has_value() || !parser_.stream_type()->IsFetch()) {
    QUICHE_BUG(quic_bug_read_one_object_parser_unexpected_state)
        << "Requesting object, parser in unexpected state";
  }
  if (!track_.IsValid()) {
    return;
  }
  UpstreamFetch* fetch =
      absl::down_cast<UpstreamFetch*>(track_.GetIfAvailable());
  UpstreamFetch::UpstreamFetchTask* task = fetch->task();
  if (task == nullptr) {
    return;
  }
  if (task->HasObject() && !task->NeedsMorePayload()) {
    return;  // The message is complete. Do not read more.
  }
  uint64_t start_length = task->payload_length();
  parser_.ReadAtMostOneObject();
  // If it read an object, it called OnObjectMessage and may have altered the
  // task's object state.
  if (task->payload_length() > start_length) {
    task->NotifyNewObject();
  }
}

void IncomingDataStream::OnCanRead() {
  if (!parser_.stream_type().has_value()) {
    parser_.ReadStreamType();
    if (!parser_.stream_type().has_value()) {
      return;
    }
  }
  if (parser_.stream_type()->IsPadding()) {
    (void)stream_->SkipBytes(stream_->ReadableBytes());
    return;
  }
  bool knew_track_alias = parser_.track_alias().has_value();
  if (!knew_track_alias) {
    parser_.ReadTrackAlias();
    if (!parser_.track_alias().has_value()) {
      return;
    }
  }
  QUICHE_CHECK(parser_.stream_type().has_value());
  QUICHE_CHECK(parser_.track_alias().has_value());
  if (parser_.stream_type()->IsSubgroup()) {
    if (!knew_track_alias) {
      track_ = session_->GetSubscribe(*parser_.track_alias());
      // This is a new stream for a subscribe. Notify the subscription.
      LiveSubscriber* subscribe =
          absl::down_cast<LiveSubscriber*>(track_.GetIfAvailable());
      if (subscribe == nullptr) {
        stream_->SendStopSending(kResetCodeCancelled);
        return;
      }
      subscribe->OnStreamOpened();
      parser_.set_default_publisher_priority(
          subscribe->default_publisher_priority());
      visitor_ = subscribe->visitor();
    }
    parser_.ReadAllData();
    return;
  }
  // FETCH
  if (!knew_track_alias) {
    track_ = session_->GetFetch(*parser_.track_alias());
  }
  if (!track_.IsValid()) {
    stream_->SendStopSending(kResetCodeCancelled);
    return;
  }
  UpstreamFetch* fetch =
      absl::down_cast<UpstreamFetch*>(track_.GetIfAvailable());
  if (!knew_track_alias) {
    // If the task already exists (FETCH_OK has arrived), the callback will
    // immediately execute to read the first object. Otherwise, it will only
    // execute when the task is created or a cached object is read.
    fetch->OnStreamOpened([this]() { MaybeReadOneObject(); });
    return;
  }
  MaybeReadOneObject();
}

void IncomingDataStream::OnParsingError(MoqtError error_code,
                                        absl::string_view reason) {
  session_->Error(error_code, absl::StrCat("Parse error: ", reason));
}

}  // namespace moqt
