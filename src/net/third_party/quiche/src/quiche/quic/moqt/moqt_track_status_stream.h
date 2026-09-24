// Copyright 2026 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef QUICHE_QUIC_MOQT_MOQT_TRACK_STATUS_STREAM_H_
#define QUICHE_QUIC_MOQT_MOQT_TRACK_STATUS_STREAM_H_

#include <cstdint>
#include <memory>
#include <optional>

#include "absl/base/nullability.h"
#include "absl/status/status.h"
#include "quiche/quic/moqt/moqt_bidi_stream.h"
#include "quiche/quic/moqt/moqt_error.h"
#include "quiche/quic/moqt/moqt_fetch_task.h"
#include "quiche/quic/moqt/moqt_framer.h"
#include "quiche/quic/moqt/moqt_key_value_pair.h"
#include "quiche/quic/moqt/moqt_live_publisher.h"
#include "quiche/quic/moqt/moqt_messages.h"
#include "quiche/quic/moqt/moqt_names.h"
#include "quiche/quic/moqt/moqt_parser.h"
#include "quiche/quic/moqt/moqt_priority.h"
#include "quiche/quic/moqt/moqt_publisher.h"
#include "quiche/quic/moqt/moqt_types.h"
#include "quiche/common/quiche_weak_ptr.h"
#include "quiche/web_transport/web_transport.h"

namespace moqt {

// MoqtTrackStatusRequestStream represents an outgoing TRACK_STATUS request.
class MoqtTrackStatusRequestStream : public MoqtBidiStreamBase {
 public:
  MoqtTrackStatusRequestStream(MoqtFramer* absl_nonnull framer,
                               const MoqtControlMessageParser& message_parser,
                               uint64_t request_id,
                               const FullTrackName& full_track_name,
                               const MessageParameters& parameters,
                               SessionErrorCallback session_error_callback,
                               MoqtResponseCallback response_callback);
  ~MoqtTrackStatusRequestStream() { Detach(); }

  // MoqtBidiStreamBase overrides.
  void OnStreamBound() override;
  absl::Status OnRawControlMessage(
      const MoqtRawControlMessage& message) override;
  absl::Status OnControlMessage(const MoqtRequestOk& message);
  absl::Status OnControlMessage(const MoqtRequestError& message);

  void Detach() override;

 private:
  const uint64_t request_id_;
  const FullTrackName full_track_name_;
  const MessageParameters parameters_;
  MoqtResponseCallback response_callback_;
};

// MoqtTrackStatusResponseStream represents an incoming TRACK_STATUS request.
class MoqtTrackStatusResponseStream : public MoqtBidiStreamBase,
                                      public MoqtObjectListener {
 public:
  MoqtTrackStatusResponseStream(
      MoqtFramer* absl_nonnull framer,
      const MoqtControlMessageParser& message_parser,
      SessionErrorCallback session_error_callback,
      quiche::QuicheWeakPtr<SessionToPublisherInterface> session);
  ~MoqtTrackStatusResponseStream() { Detach(); }

  // MoqtBidiStreamBase overrides.
  void OnStreamBound() override { stream_parser()->set_allow_fin(true); }
  absl::Status OnRawControlMessage(
      const MoqtRawControlMessage& message) override;
  absl::Status OnControlMessage(const MoqtTrackStatus& message);

  // MoqtObjectListener overrides.
  void OnSubscribeAccepted() override;
  void OnSubscribeRejected(MoqtRequestErrorInfo info) override;
  void OnNewObjectAvailable(Location, std::optional<uint64_t>,
                            MoqtPriority) override {}
  void OnNewFinAvailable(Location, uint64_t) override {}
  void OnSubgroupAbandoned(uint64_t, uint64_t,
                           webtransport::StreamErrorCode) override {}
  void OnGroupAbandoned(uint64_t) override {}
  void OnTrackPublisherGone() override;

  void Detach() override;

 private:
  SessionToPublisherInterface* absl_nullable session() const {
    return session_.GetIfAvailable();
  }

  std::optional<uint64_t> request_id_;
  const quiche::QuicheWeakPtr<SessionToPublisherInterface> session_;
  std::shared_ptr<MoqtTrackPublisher> publisher_ = nullptr;
};

}  // namespace moqt

#endif  // QUICHE_QUIC_MOQT_MOQT_TRACK_STATUS_STREAM_H_
