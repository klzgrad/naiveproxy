// Copyright (c) 2026 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef QUICHE_QUIC_MOQT_MOQT_SUBSCRIBE_STREAM_H_
#define QUICHE_QUIC_MOQT_MOQT_SUBSCRIBE_STREAM_H_

#include <cstdint>
#include <memory>

#include "absl/base/nullability.h"
#include "absl/status/status.h"
#include "quiche/quic/core/quic_alarm_factory.h"
#include "quiche/quic/core/quic_clock.h"
#include "quiche/quic/moqt/moqt_bidi_stream.h"
#include "quiche/quic/moqt/moqt_framer.h"
#include "quiche/quic/moqt/moqt_key_value_pair.h"
#include "quiche/quic/moqt/moqt_live_publisher.h"
#include "quiche/quic/moqt/moqt_messages.h"
#include "quiche/quic/moqt/moqt_names.h"
#include "quiche/quic/moqt/moqt_object_subscriber.h"
#include "quiche/quic/moqt/moqt_parser.h"
#include "quiche/quic/moqt/moqt_session_callbacks.h"
#include "quiche/common/quiche_weak_ptr.h"

namespace moqt {

class MoqtSubscribeRequestStream : public MoqtBidiStreamBase {
 public:
  MoqtSubscribeRequestStream(
      MoqtFramer* absl_nonnull framer,
      const MoqtControlMessageParser& message_parser, uint64_t request_id,
      SessionErrorCallback session_error_callback, const FullTrackName& name,
      SubscribeVisitor* absl_nonnull visitor,
      const MessageParameters& parameters,
      LiveSubscriber::AddCallback add_callback,
      LiveSubscriber::RemoveCallback remove_callback,
      const quic::QuicClock* absl_nonnull clock,
      quic::QuicAlarmFactory* absl_nonnull alarm_factory);
  ~MoqtSubscribeRequestStream() { Detach(); }

  // StreamBase overrides.
  void OnStreamBound() override;
  absl::Status OnRawControlMessage(
      const MoqtRawControlMessage& message) override;
  absl::Status OnControlMessage(const MoqtRequestOk& message);
  absl::Status OnControlMessage(const MoqtRequestError& message);
  absl::Status OnControlMessage(const MoqtSubscribeOk& message);
  absl::Status OnControlMessage(const MoqtPublishDone& message);

  LiveSubscriber* track() const { return track_.get(); }
  void Detach() override;

 private:
  std::unique_ptr<LiveSubscriber> track_;
  LiveSubscriber::AddCallback add_callback_;
  LiveSubscriber::RemoveCallback remove_callback_;
  const quic::QuicClock* clock_;
  quic::QuicAlarmFactory* alarm_factory_;
};

class MoqtSubscribeResponseStream : public MoqtBidiStreamBase {
 public:
  MoqtSubscribeResponseStream(
      MoqtFramer* absl_nonnull framer,
      const MoqtControlMessageParser& message_parser, uint64_t track_alias,
      LivePublisher::AddCallback add_callback,
      LivePublisher::RemoveCallback remove_callback,
      SessionErrorCallback session_error_callback,
      quiche::QuicheWeakPtr<SessionToPublisherInterface> session);
  ~MoqtSubscribeResponseStream() {
    if (subscription_ != nullptr) {
      subscription_->IgnoreResetAllStreams();
    }
    Detach();
  }

  // MoqtBidiStreamBase overrides.
  void OnStreamBound() override { stream_parser()->set_allow_fin(true); }
  absl::Status OnRawControlMessage(
      const MoqtRawControlMessage& message) override;

  absl::Status OnControlMessage(const MoqtSubscribe& message);
  absl::Status OnControlMessage(const MoqtRequestUpdate& message);
  absl::Status OnControlMessage(const MoqtObjectAck& message) {
    subscription_->ProcessObjectAck(message);
    return absl::OkStatus();
  }
  void Detach() override;

 private:
  // Returns nullptr if MoqtSession is gone.
  SessionToPublisherInterface* absl_nullable session() const {
    return session_.GetIfAvailable();
  }

  uint64_t track_alias_;
  std::unique_ptr<LivePublisher> subscription_;
  LivePublisher::AddCallback add_callback_;
  LivePublisher::RemoveCallback remove_callback_;
  quiche::QuicheWeakPtr<SessionToPublisherInterface> session_;
};

}  // namespace moqt

#endif  // QUICHE_QUIC_MOQT_MOQT_SUBSCRIBE_STREAM_H_
