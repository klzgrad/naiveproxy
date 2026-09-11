// Copyright (c) 2026 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "quiche/quic/moqt/moqt_publish_stream.h"

#include <memory>
#include <optional>
#include <utility>
#include <variant>

#include "absl/base/nullability.h"
#include "absl/functional/overload.h"
#include "absl/status/status.h"
#include "quiche/quic/core/quic_alarm_factory.h"
#include "quiche/quic/core/quic_clock.h"
#include "quiche/quic/moqt/moqt_bidi_stream.h"
#include "quiche/quic/moqt/moqt_error.h"
#include "quiche/quic/moqt/moqt_fetch_task.h"
#include "quiche/quic/moqt/moqt_framer.h"
#include "quiche/quic/moqt/moqt_key_value_pair.h"
#include "quiche/quic/moqt/moqt_live_publisher.h"
#include "quiche/quic/moqt/moqt_messages.h"
#include "quiche/quic/moqt/moqt_object_subscriber.h"
#include "quiche/quic/moqt/moqt_parser.h"
#include "quiche/quic/moqt/moqt_session_callbacks.h"

namespace moqt {

MoqtPublishRequestStream::MoqtPublishRequestStream(
    MoqtFramer* absl_nonnull framer,
    const MoqtControlMessageParser& message_parser,
    LivePublisher::RemoveCallback stream_deleted_callback,
    SessionErrorCallback session_error_callback,
    MoqtResponseCallback response_callback)
    : MoqtBidiStreamBase(framer, message_parser,
                         std::move(session_error_callback)),
      response_callback_(std::move(response_callback)),
      stream_deleted_callback_(std::move(stream_deleted_callback)) {}

MoqtPublishRequestStream::~MoqtPublishRequestStream() {
  if (publisher_ != nullptr) {
    publisher_->IgnoreResetAllStreams();
  }
  Detach();
}

void MoqtPublishRequestStream::OnStreamBound() {
  stream_parser()->set_allow_fin(true);
  publisher_->parameters().largest_object =
      publisher_->publisher().largest_location();
  publisher_->parameters().expires = publisher_->publisher().expiration();
  SendOrBufferMessageOrFatal(framer()->SerializePublish(MoqtPublish{
      publisher_->request_id(), publisher_->publisher().GetTrackName(),
      publisher_->track_alias(), publisher_->parameters(),
      publisher_->publisher().extensions()}));
  // Use the default group order.
  publisher_->parameters().group_order =
      publisher_->publisher().extensions().default_publisher_group_order();
}

absl::Status MoqtPublishRequestStream::OnRawControlMessage(
    const MoqtRawControlMessage& message) {
  return ControlMessageDispatcher::DispatchControlMessage(
      *this, message_parser(), message, "publish publisher");
}

absl::Status MoqtPublishRequestStream::OnControlMessage(
    const MoqtRequestOk& message) {
  if (message.request_id != publisher_->request_id()) {
    return absl::InvalidArgumentError(
        "REQUEST_OK does not match PUBLISH request ID");
  }
  std::move(response_callback_)(message.parameters);
  publisher_->Update(message.parameters);
  return absl::OkStatus();
}

absl::Status MoqtPublishRequestStream::OnControlMessage(
    const MoqtRequestError& message) {
  if (message.request_id != publisher_->request_id()) {
    return absl::InvalidArgumentError(
        "REQUEST_OK does not match PUBLISH request ID");
  }
  std::move(response_callback_)(MoqtRequestErrorInfo{
      message.error_code, message.retry_interval, message.reason_phrase});
  return absl::OkStatus();
}

absl::Status MoqtPublishRequestStream::OnControlMessage(
    const MoqtRequestUpdate& message) {
  MessageParameters in_parameters = message.parameters, out_parameters;
  out_parameters.largest_object = publisher_->publisher().largest_location();
  if (in_parameters.subscription_filter.has_value()) {
    in_parameters.subscription_filter->OnLargestObject(
        out_parameters.largest_object);
  }
  publisher_->Update(in_parameters);
  CheckStatus(SendRequestOk(message.request_id, MessageParameters()));
  return absl::OkStatus();
}

MoqtPublishResponseStream::MoqtPublishResponseStream(
    MoqtFramer* absl_nonnull framer,
    const MoqtControlMessageParser& message_parser,
    const quic::QuicClock* absl_nonnull clock,
    quic::QuicAlarmFactory* absl_nonnull alarm_factory,
    SessionErrorCallback session_error_callback,
    const MoqtIncomingPublishCallback* absl_nonnull incoming_publish_callback,
    LiveSubscriber::AddCallback add_callback,
    LiveSubscriber::RemoveCallback remove_callback)
    : MoqtBidiStreamBase(framer, message_parser,
                         std::move(session_error_callback)),
      clock_(clock),
      alarm_factory_(alarm_factory),
      incoming_publish_callback_(incoming_publish_callback),
      add_callback_(std::move(add_callback)),
      remove_callback_(std::move(remove_callback)),
      weak_ptr_factory_(this) {}

absl::Status MoqtPublishResponseStream::OnRawControlMessage(
    const MoqtRawControlMessage& message) {
  return ControlMessageDispatcher::DispatchControlMessage(
      *this, message_parser(), message, "publish subscriber");
}

absl::Status MoqtPublishResponseStream::OnControlMessage(
    const MoqtPublish& message) {
  if (add_callback_ == nullptr) {
    // Two PUBLISH messages for the same stream.
    return absl::InvalidArgumentError("Multiple PUBLISH on the same stream");
  }
  subscriber_ = std::make_unique<LiveSubscriber>(message, nullptr, this);
  if (!std::move(add_callback_)(subscriber_.get())) {
    add_callback_ = nullptr;
    return SendRequestError(message.request_id,
                            RequestErrorCode::kDuplicateSubscription,
                            /*retry_interval=*/std::nullopt, "",
                            /*fin=*/true);
  }
  add_callback_ = nullptr;
  if (subscriber_->visitor() == nullptr) {
    // There was no existing SUBSCRIBE, so invoke the callback.
    subscriber_->set_visitor((*incoming_publish_callback_)(
        message.full_track_name, message.parameters, message.extensions,
        [weakptr = weak_ptr_factory_.Create(), request_id = message.request_id](
            const std::variant<MessageParameters, MoqtRequestErrorInfo>
                response) {
          MoqtPublishResponseStream* stream = weakptr.GetIfAvailable();
          if (stream == nullptr) {
            return;
          }
          std::visit(
              absl::Overload{[&](const MessageParameters& parameters) {
                               stream->subscriber_->Update(parameters);
                               stream->CheckStatus(stream->SendRequestOk(
                                   request_id, parameters, /*fin=*/false));
                             },
                             [&](const MoqtRequestErrorInfo& error_info) {
                               stream->CheckStatus(stream->SendRequestError(
                                   request_id, error_info, /*fin=*/true));
                             }},
              response);
        }));
  } else {
    // Since the application already called SUBSCRIBE, there will be no
    // invocation of the request callback. Send REQUEST_OK immediately.
    CheckStatus(
        SendRequestOk(message.request_id, subscriber_->const_parameters()));
  }
  incoming_publish_callback_ = nullptr;
  if (subscriber_->visitor() == nullptr) {
    // The application doesn't care.
    CheckStatus(SendRequestError(message.request_id,
                                 RequestErrorCode::kUninterested,
                                 /*retry_interval=*/std::nullopt, "",
                                 /*fin=*/true));
    return absl::OkStatus();
  }
  // Notify the visitor.
  subscriber_->OnObjectOrOk(
      SubscribeOkData{message.parameters, message.extensions});
  return absl::OkStatus();
}

absl::Status MoqtPublishResponseStream::OnControlMessage(
    const MoqtRequestUpdate& message) {
  if (subscriber_ == nullptr) {
    // Stream is already closing.
    return absl::OkStatus();
  }
  subscriber_->Update(message.parameters);
  CheckStatus(SendRequestOk(message.request_id, MessageParameters()));
  return absl::OkStatus();
}

absl::Status MoqtPublishResponseStream::OnControlMessage(
    const MoqtRequestOk& message) {
  // TODO(martinduke): Process REQUEST_OK parameters.
  return request_update_queue().OnControlMessage(message);
}

absl::Status MoqtPublishResponseStream::OnControlMessage(
    const MoqtRequestError& message) {
  // TODO(martinduke): Process REQUEST_OK parameters.
  absl::Status status = request_update_queue().OnControlMessage(message);
  if (status.ok()) {
    Fin();
  }
  return status;
}

absl::Status MoqtPublishResponseStream::OnControlMessage(
    const MoqtPublishDone& message) {
  if (subscriber_ == nullptr) {
    // PUBLISH_DONE can be sent before the subscriber rejects the track.
    return absl::OkStatus();
  }
  subscriber_->OnPublishDone(message.stream_count, clock_, alarm_factory_);
  return absl::OkStatus();
}

}  // namespace moqt
