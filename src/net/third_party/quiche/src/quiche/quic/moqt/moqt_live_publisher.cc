// Copyright (c) 2026 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "quiche/quic/moqt/moqt_live_publisher.h"

#include <algorithm>
#include <cstdint>
#include <memory>
#include <optional>
#include <utility>
#include <vector>

#include "absl/base/casts.h"
#include "absl/base/nullability.h"
#include "absl/container/btree_map.h"
#include "absl/container/flat_hash_set.h"
#include "absl/strings/string_view.h"
#include "quiche/quic/core/quic_clock.h"
#include "quiche/quic/moqt/moqt_bidi_stream.h"
#include "quiche/quic/moqt/moqt_error.h"
#include "quiche/quic/moqt/moqt_framer.h"
#include "quiche/quic/moqt/moqt_key_value_pair.h"
#include "quiche/quic/moqt/moqt_messages.h"
#include "quiche/quic/moqt/moqt_object.h"
#include "quiche/quic/moqt/moqt_priority.h"
#include "quiche/quic/moqt/moqt_publisher.h"
#include "quiche/quic/moqt/moqt_stream_map.h"
#include "quiche/quic/moqt/moqt_trace_recorder.h"
#include "quiche/quic/moqt/moqt_types.h"
#include "quiche/quic/moqt/moqt_uni_stream.h"
#include "quiche/common/quiche_buffer_allocator.h"
#include "quiche/common/quiche_weak_ptr.h"
#include "quiche/web_transport/web_transport.h"

namespace moqt {

LivePublisher::LivePublisher(
    MoqtFramer framer, std::shared_ptr<MoqtTrackPublisher> track_publisher,
    MoqtBidiStreamBase* absl_nonnull bidi_stream, uint64_t request_id,
    uint64_t track_alias, const MessageParameters& parameters,
    quiche::QuicheWeakPtr<SessionToPublisherInterface> visitor, bool is_publish)
    : track_publisher_(track_publisher),
      bidi_stream_(bidi_stream),
      visitor_(visitor),
      request_id_(request_id),
      established_(is_publish),
      track_alias_(track_alias),
      framer_(framer),
      parameters_(parameters),
      weak_ptr_factory_(this) {
  SessionToPublisherInterface* session_info = visitor_.GetIfAvailable();
  if (session_info == nullptr) {
    return;
  }
  monitoring_interface_ = session_info->ReleaseMonitoringInterface(
      track_publisher_->GetTrackName());
  if (monitoring_interface_ == nullptr && track_publisher_ != nullptr) {
    monitoring_interface_ = track_publisher_->GetMonitoringInterface();
  }
  if (monitoring_interface_ != nullptr) {
    monitoring_interface_->OnObjectAckSupportKnown(parameters.oack_window_size);
  }
  QUIC_DLOG(INFO) << "Created subscription for "
                  << track_publisher_->GetTrackName();
  // TODO(martinduke): Handle NEW_GROUP_REQUEST
}

LivePublisher::~LivePublisher() {
  if (track_publisher_ != nullptr) {
    track_publisher_->RemoveObjectListener(this);
  }
}

void LivePublisher::Update(const MessageParameters& parameters) {
  // TODO(martinduke): If there are auth tokens, this probably has to go to the
  // application.
  // TODO(martinduke): If the subscribe window has shrunk, close any streams
  // that are now outside the window. Also send PUBLISH_DONE if now done.
  MoqtPriority old_priority =
      parameters_.subscriber_priority.value_or(kDefaultSubscriberPriority);
  parameters_.Update(parameters);
  if (parameters.subscriber_priority.has_value()) {  // priority changed.
    MoqtPriority new_priority = *parameters.subscriber_priority;
    // Reprioritize all active streams.
    for (const webtransport::StreamId stream_id : stream_map_.GetAllStreams()) {
      webtransport::Stream* stream = GetStreamById(stream_id);
      if (stream == nullptr) {
        continue;
      }
      OutgoingSubgroupStream* outgoing_stream =
          absl::down_cast<OutgoingSubgroupStream*>(stream->visitor());
      outgoing_stream->UpdatePriority(new_priority);
    }
    if (pending_streams_.empty()) {
      return;
    }
    // Tell the session that pending stream priority has changed.
    MoqtPriority publisher_priority =
        pending_streams_.rbegin()->second.publisher_priority.value_or(
            track_publisher_->extensions().default_publisher_priority());
    MoqtTrackPriority old_track_priority = {old_priority, publisher_priority};
    if (visitor() == nullptr) {
      return;
    }
    visitor()->UpdateTrackPriority(
        request_id_, old_track_priority,
        MoqtTrackPriority{new_priority, publisher_priority});
    // Don't bother to update all the pending stream send orders.
  }
}

void LivePublisher::ResetAllStreams() {
  if (ignore_reset_all_streams_) {
    return;
  }
  for (const webtransport::StreamId stream_id : stream_map_.GetAllStreams()) {
    webtransport::Stream* stream = GetStreamById(stream_id);
    if (stream != nullptr) {
      stream->ResetWithUserCode(kResetCodeCancelled);
    }
  }
}

void LivePublisher::OnSubscribeAccepted() {
  if (established_) {
    return;  // It's a PUBLISH.
  }
  established_ = true;
  parameters_.largest_object = track_publisher_->largest_location();
  if (parameters_.subscription_filter.has_value()) {
    parameters_.subscription_filter->OnLargestObject(
        parameters_.largest_object);
  }
  MoqtSubscribeOk subscribe_ok;
  subscribe_ok.request_id = request_id_;
  subscribe_ok.track_alias = track_alias_;
  subscribe_ok.parameters.expires = track_publisher_->expiration();
  subscribe_ok.parameters.largest_object = parameters_.largest_object;
  subscribe_ok.extensions = track_publisher_->extensions();
  if (!parameters_.group_order.has_value()) {
    parameters_.group_order =
        subscribe_ok.extensions.default_publisher_group_order();
  }
  // TODO(martinduke): Support sending DELIVERY_TIMEOUT parameter as the
  // publisher.
  default_publisher_priority_ =
      subscribe_ok.extensions.default_publisher_priority();
  bidi_stream_->SendOrBufferMessageOrFatal(
      framer_.SerializeSubscribeOk(subscribe_ok));
  // TODO(martinduke): If we buffer objects that arrived previously, the arrival
  // of the track alias disambiguates what subscription they belong to. Send
  // them.
}

void LivePublisher::OnSubscribeRejected(MoqtRequestErrorInfo info) {
  bidi_stream_->CheckStatus(bidi_stream_->SendRequestError(request_id_, info,
                                                           /*fin=*/true));
  // Sending FIN will delete the class.
}

void LivePublisher::OnNewObjectAvailable(Location location,
                                         std::optional<uint64_t> subgroup,
                                         MoqtPriority publisher_priority) {
  if (!InWindow(location)) {
    return;
  }

  if (monitoring_interface_ != nullptr) {
    // Notify the monitoring interface about all newly published normal objects.
    // Objects with other statuses are not guaranteed to be acknowledged, thus
    // passing them into the monitoring interface can lead to confusion.
    std::optional<PublishedObject> object = track_publisher_->GetCachedObject(
        location.group, subgroup, location.object);
    QUICHE_DCHECK(object.has_value())
        << "Object " << absl::StrCat(location) << " on track "
        << track_publisher_->GetTrackName().ToString()
        << " does not exist, despite OnNewObjectAvailable being called";
    if (object.has_value() && object->metadata.location == location &&
        object->metadata.status == MoqtObjectStatus::kNormal) {
      monitoring_interface_->OnNewObjectEnqueued(location);
    }
  }

  // TODO(vasilvv): This currently sends UINT64_MAX for datagram subgroups.
  // Maybe do something more satisfactory?
  SessionToPublisherInterface* session_info = visitor();
  if (session_info == nullptr) {
    return;  // Session is gone.
  }
  session_info->trace_recorder().RecordNewObjectAvaliable(
      track_alias_, *track_publisher_, location, subgroup.value_or(UINT64_MAX),
      publisher_priority);

  std::optional<webtransport::StreamId> stream_id;
  if (subgroup.has_value()) {
    DataStreamIndex index(location.group, *subgroup);
    if (reset_subgroups_.contains(index)) {
      // This subgroup has already been reset, ignore.
      return;
    }
    stream_id = stream_map_.GetStreamFor(index);
  }
  if (session_info->alternate_delivery_timeout() &&
      !delivery_timeout().IsInfinite() && largest_sent_.has_value() &&
      location.group >= largest_sent_->group) {
    // Start the delivery timeout timer on all previous groups.
    for (uint64_t group = first_active_group_; group < location.group;
         ++group) {
      for (webtransport::StreamId stream_to_update :
           stream_map_.GetStreamsForGroup(group)) {
        webtransport::Stream* raw_stream = GetStreamById(stream_to_update);
        if (raw_stream == nullptr) {
          continue;
        }
        OutgoingSubgroupStream* stream =
            absl::down_cast<OutgoingSubgroupStream*>(raw_stream->visitor());
        stream->CreateAndSetAlarm(session_info->clock()->ApproximateNow() +
                                  delivery_timeout());
      }
    }
  }
  QUICHE_DCHECK_GE(location.group, first_active_group_);
  if (!subgroup.has_value()) {
    SendDatagram(location);
    return;
  }

  webtransport::Stream* raw_stream = nullptr;
  if (stream_id.has_value()) {
    raw_stream = GetStreamById(*stream_id);
    if (raw_stream != nullptr) {
      raw_stream->visitor()->OnCanWrite();
    }
    return;
  }
  NewDataStreamParameters parameters(
      location.group, *subgroup, location.object,
      publisher_priority == default_publisher_priority_
          ? std::nullopt
          : std::make_optional(publisher_priority));
  raw_stream = OpenDataStream(parameters);
  if (raw_stream == nullptr) {
    StreamRank rank = StreamRankFor(parameters);
    if (pending_streams_.empty() || rank > pending_streams_.rbegin()->first) {
      session_info->UpdateTrackPriority(
          request_id_,
          /*old_priority=*/pending_streams_.empty()
              ? std::optional<MoqtTrackPriority>()
              : std::make_optional(
                    MoqtTrackPriority{subscriber_priority(),
                                      pending_streams_.rbegin()
                                          ->second.publisher_priority.value_or(
                                              default_publisher_priority())}),
          MoqtTrackPriority{subscriber_priority(), publisher_priority});
    }
    pending_streams_.emplace(rank, parameters);
  }
}

void LivePublisher::OnTrackPublisherGone() {
  PublishIsDone(PublishDoneCode::kGoingAway, "Publisher is gone");
}

// TODO(martinduke): Revise to check if the last object has been delivered.
void LivePublisher::OnNewFinAvailable(Location location, uint64_t subgroup) {
  if (!InWindow(location.group)) {
    return;
  }
  DataStreamIndex index(location.group, subgroup);
  std::optional<webtransport::StreamId> stream_id =
      stream_map_.GetStreamFor(index);
  if (!stream_id.has_value()) {
    return;
  }
  webtransport::Stream* raw_stream = GetStreamById(*stream_id);
  if (raw_stream == nullptr) {
    return;
  }
  OutgoingSubgroupStream* stream =
      absl::down_cast<OutgoingSubgroupStream*>(raw_stream->visitor());
  stream->Fin(location);
  // Sending FIN will delete the class.
}

void LivePublisher::OnSubgroupAbandoned(
    uint64_t group, uint64_t subgroup,
    webtransport::StreamErrorCode error_code) {
  if (!InWindow(group)) {
    return;
  }
  DataStreamIndex index(group, subgroup);
  if (reset_subgroups_.contains(index)) {
    // This subgroup has already been reset, ignore.
    return;
  }
  reset_subgroups_.insert(index);
  QUICHE_DCHECK_GE(group, first_active_group_);
  std::optional<webtransport::StreamId> stream_id =
      stream_map_.GetStreamFor(index);
  if (!stream_id.has_value()) {
    return;
  }
  webtransport::Stream* raw_stream = GetStreamById(*stream_id);
  if (raw_stream == nullptr) {
    return;
  }
  raw_stream->ResetWithUserCode(error_code);
}

void LivePublisher::OnGroupAbandoned(uint64_t group_id) {
  if (!InWindow(group_id)) {
    // The group is not in the window, ignore.
    return;
  }
  std::vector<webtransport::StreamId> streams =
      stream_map_.GetStreamsForGroup(group_id);
  if (delivery_timeout().IsInfinite() && largest_sent_.has_value() &&
      largest_sent_->group <= group_id) {
    PublishIsDone(PublishDoneCode::kTooFarBehind, "");
    // No class access below this line!
    return;
  }
  for (webtransport::StreamId stream_id : streams) {
    webtransport::Stream* raw_stream = GetStreamById(stream_id);
    if (raw_stream == nullptr) {
      continue;
    }
    raw_stream->ResetWithUserCode(kResetCodeDeliveryTimeout);
    // Sending the Reset will call the destructor for OutgoingSubgroupStream,
    // which will erase it from the SendStreamMap.
  }
  first_active_group_ = std::max(first_active_group_, group_id + 1);
  absl::erase_if(reset_subgroups_, [&](const DataStreamIndex& index) {
    return index.group < first_active_group_;
  });
}

void LivePublisher::SendDatagram(Location sequence) {
  std::optional<PublishedObject> object = track_publisher_->GetCachedObject(
      sequence.group, std::nullopt, sequence.object);
  if (!object.has_value()) {
    QUICHE_BUG(PublishedSubscription_SendDatagram_object_not_in_cache)
        << "Got notification about an object that is not in the cache";
    return;
  }
  MoqtObject header;
  header.track_alias = track_alias_;
  header.group_id = object->metadata.location.group;
  header.object_id = object->metadata.location.object;
  header.publisher_priority = object->metadata.publisher_priority;
  header.extension_headers = object->metadata.extensions;
  header.object_status = object->metadata.status;
  header.subgroup_id = std::nullopt;
  header.payload_length = object->metadata.payload_length;
  QUICHE_BUG_IF(LivePublisher_SendDatagram_partial_payload,
                object->payload.size() > 1)
      << "Datagram is split into multiple slices";
  quiche::QuicheBuffer datagram = framer_.SerializeObjectDatagram(
      header, object->payload[0].AsStringView(),
      default_publisher_priority_.value_or(kDefaultPublisherPriority));
  if (visitor() == nullptr) {
    return;
  }
  visitor()->session()->SendOrQueueDatagram(datagram.AsStringView());
  OnObjectSent(object->metadata.location);
}

void LivePublisher::ProcessObjectAck(const MoqtObjectAck& message) {
  SessionToPublisherInterface* session_info = visitor();
  if (session_info == nullptr) {
    return;
  }
  session_info->trace_recorder().RecordObjectAck(
      track_alias_, Location(message.group_id, message.object_id),
      message.delta_from_deadline);
  if (monitoring_interface_ != nullptr) {
    monitoring_interface_->OnObjectAckReceived(
        Location(message.group_id, message.object_id),
        message.delta_from_deadline);
  }
}

webtransport::Stream* absl_nullable LivePublisher::OpenDataStream(
    const NewDataStreamParameters& parameters) {
  SessionToPublisherInterface* session_info = visitor();
  if (session_info == nullptr) {
    return nullptr;
  }
  if (!session_info->session()->CanOpenNextOutgoingUnidirectionalStream()) {
    return nullptr;
  }
  webtransport::Stream* new_stream =
      session_info->session()->OpenOutgoingUnidirectionalStream();
  if (new_stream == nullptr) {
    return nullptr;
  }
  stream_map_.AddStream(parameters.index, new_stream->GetStreamId());
  new_stream->SetVisitor(std::make_unique<OutgoingSubgroupStream>(
      framer_, new_stream, parameters.index, parameters.first_object,
      weak_ptr_factory_.Create(), track_publisher_,
      StreamPriorityFor(parameters), track_alias_,
      &session_info->trace_recorder()));
  ++streams_opened_;
  new_stream->visitor()->OnCanWrite();
  return new_stream;
}

void LivePublisher::PublishIsDone(PublishDoneCode code,
                                  absl::string_view error_reason) {
  MoqtPublishDone publish_done;
  publish_done.request_id = request_id_;
  publish_done.status_code = code;
  publish_done.stream_count = streams_opened_;
  publish_done.error_reason = error_reason;
  // TODO(martinduke): It is technically correct, but not good, to reset all the
  // streams in order to send PUBLISH_DONE. It's better to wait until streams
  // FIN naturally, where possible.
  QUICHE_DLOG(INFO) << "Sending PUBLISH_DONE message for "
                    << track_publisher_->GetTrackName();
  bidi_stream_->SendOrBufferMessageOrFatal(
      framer_.SerializePublishDone(publish_done), /*fin=*/true);
  // sending FIN will delete the class.
}

void LivePublisher::OnDataStreamDestroyed(DataStreamIndex end_sequence) {
  stream_map_.RemoveStream(end_sequence);
}

void LivePublisher::OnCanCreateNewUniStream() {
  SessionToPublisherInterface* session_info = visitor();
  if (session_info == nullptr) {
    return;
  }
  while (visitor_.IsValid() &&
         session_info->session()->CanOpenNextOutgoingUnidirectionalStream()) {
    auto it = pending_streams_.rbegin();
    while (it != pending_streams_.rend() &&
           (it->second.index.group < first_active_group_ ||
            reset_subgroups_.contains(it->second.index))) {
      pending_streams_.erase(--(it.base()));
      it = pending_streams_.rbegin();
    }
    if (it == pending_streams_.rend()) {
      return;
    }
    if (OpenDataStream(it->second) == nullptr) {
      return;
    }
    pending_streams_.erase(--(it.base()));
    if (!pending_streams_.empty()) {
      session_info->UpdateTrackPriority(
          request_id_, std::nullopt,
          MoqtTrackPriority{
              subscriber_priority(),
              pending_streams_.rbegin()->second.publisher_priority.value_or(
                  default_publisher_priority())});
    }
  }
}

void LivePublisher::OnObjectSent(Location sequence) {
  if (largest_sent_.has_value()) {
    largest_sent_ = std::max(*largest_sent_, sequence);
  } else {
    largest_sent_ = sequence;
  }
  // TODO: send PUBLISH_DONE if the subscription is done.
}

}  // namespace moqt
