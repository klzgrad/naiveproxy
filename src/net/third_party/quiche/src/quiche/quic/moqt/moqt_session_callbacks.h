// Copyright 2023 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef QUICHE_QUIC_MOQT_MOQT_SESSION_CALLBACKS_H_
#define QUICHE_QUIC_MOQT_MOQT_SESSION_CALLBACKS_H_

#include <optional>

#include "absl/strings/string_view.h"
#include "quiche/quic/core/quic_clock.h"
#include "quiche/quic/core/quic_default_clock.h"
#include "quiche/quic/moqt/moqt_messages.h"
#include "quiche/common/quiche_callbacks.h"

namespace moqt {

// Called when the SETUP message from the peer is received.
using MoqtSessionEstablishedCallback = quiche::SingleUseCallback<void()>;

// Called when a GOAWAY message is received from the server.
using MoqtSessionGoAwayCallback =
    quiche::SingleUseCallback<void(absl::string_view new_session_uri)>;

// Called when the session is terminated.
using MoqtSessionTerminatedCallback =
    quiche::SingleUseCallback<void(absl::string_view error_message)>;

// Called from the session destructor.
using MoqtSessionDeletedCallback = quiche::SingleUseCallback<void()>;

// Called whenever an ANNOUNCE or UNANNOUNCE message is received from the peer.
// ANNOUNCE sets a value for |parameters|, UNANNOUNCE does not.
using MoqtIncomingAnnounceCallback =
    quiche::MultiUseCallback<std::optional<MoqtAnnounceErrorReason>(
        const FullTrackName& track_namespace,
        const std::optional<VersionSpecificParameters>& parameters)>;

// Called whenever SUBSCRIBE_ANNOUNCES or UNSUBSCRIBE_ANNOUNCES is received from
// the peer.  For SUBSCRIBE_ANNOUNCES, the return value indicates whether to
// return an OK or an ERROR; for UNSUBSCRIBE_ANNOUNCES, the return value is
// ignored. SUBSCRIBE_ANNOUNCES sets a value for |parameters|,
// UNSUBSCRIBE_ANNOUNCES does not.
using MoqtIncomingSubscribeAnnouncesCallback =
    quiche::MultiUseCallback<std::optional<MoqtSubscribeErrorReason>(
        const FullTrackName& track_namespace,
        std::optional<VersionSpecificParameters> parameters)>;

inline std::optional<MoqtAnnounceErrorReason> DefaultIncomingAnnounceCallback(
    const FullTrackName& /*track_namespace*/,
    std::optional<VersionSpecificParameters> /*parameters*/) {
  return std::optional(MoqtAnnounceErrorReason{
      RequestErrorCode::kNotSupported,
      "This endpoint does not accept incoming ANNOUNCE messages"});
};

inline std::optional<MoqtSubscribeErrorReason>
DefaultIncomingSubscribeAnnouncesCallback(
    const FullTrackName& track_namespace,
    std::optional<VersionSpecificParameters> /*parameters*/) {
  return MoqtSubscribeErrorReason{
      RequestErrorCode::kNotSupported,
      "This endpoint does not support incoming SUBSCRIBE_ANNOUNCES messages"};
}

// Callbacks for session-level events.
struct MoqtSessionCallbacks {
  MoqtSessionEstablishedCallback session_established_callback = +[] {};
  MoqtSessionGoAwayCallback goaway_received_callback =
      +[](absl::string_view) {};
  MoqtSessionTerminatedCallback session_terminated_callback =
      +[](absl::string_view) {};
  MoqtSessionDeletedCallback session_deleted_callback = +[] {};

  MoqtIncomingAnnounceCallback incoming_announce_callback =
      DefaultIncomingAnnounceCallback;
  MoqtIncomingSubscribeAnnouncesCallback incoming_subscribe_announces_callback =
      DefaultIncomingSubscribeAnnouncesCallback;
  const quic::QuicClock* clock = quic::QuicDefaultClock::Get();
};

}  // namespace moqt

#endif  // QUICHE_QUIC_MOQT_MOQT_SESSION_CALLBACKS_H_
