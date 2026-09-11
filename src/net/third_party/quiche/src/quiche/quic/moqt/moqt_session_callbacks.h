// Copyright 2023 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef QUICHE_QUIC_MOQT_MOQT_SESSION_CALLBACKS_H_
#define QUICHE_QUIC_MOQT_MOQT_SESSION_CALLBACKS_H_

#include <cstdint>
#include <memory>
#include <optional>
#include <utility>
#include <variant>

#include "absl/base/nullability.h"
#include "absl/strings/string_view.h"
#include "quiche/quic/core/quic_clock.h"
#include "quiche/quic/core/quic_default_clock.h"
#include "quiche/quic/core/quic_time.h"
#include "quiche/quic/moqt/moqt_error.h"
#include "quiche/quic/moqt/moqt_fetch_task.h"
#include "quiche/quic/moqt/moqt_key_value_pair.h"
#include "quiche/quic/moqt/moqt_names.h"
#include "quiche/quic/moqt/moqt_object.h"
#include "quiche/quic/moqt/moqt_types.h"
#include "quiche/common/quiche_callbacks.h"

namespace moqt {

using MoqtObjectAckFunction =
    quiche::MultiUseCallback<void(uint64_t group_id, uint64_t object_id,
                                  quic::QuicTimeDelta delta_from_deadline)>;

struct SubscribeOkData {
  MessageParameters parameters;
  TrackExtensions extensions;
};

class SubscribeVisitor {
 public:
  virtual ~SubscribeVisitor() = default;
  // Called when the session receives a response to the SUBSCRIBE.
  virtual void OnReply(
      const FullTrackName& full_track_name,
      std::variant<SubscribeOkData, MoqtRequestErrorInfo> response) = 0;
  // Called when the subscription process is far enough that it is possible to
  // send OBJECT_ACK messages; provides a callback to do so. The callback is
  // valid for as long as the session is valid.
  virtual void OnCanAckObjects(MoqtObjectAckFunction ack_function) = 0;
  // Called when an object fragment (or an entire object) is received.
  virtual void OnObjectFragment(const FullTrackName& full_track_name,
                                const PublishedObjectMetadata& metadata,
                                absl::string_view object, uint64_t offset) = 0;
  // Called when the subscription state goes away, regardless of whether or not
  // there was a PUBLISH_DONE message.
  virtual void OnPublishDone(FullTrackName full_track_name) = 0;
  // Called when the track is malformed per Section 2.5 of
  // draft-ietf-moqt-moq-transport-12. If the application is a relay, it MUST
  // terminate downstream delivery of the track.
  virtual void OnMalformedTrack(const FullTrackName& full_track_name) = 0;

  // End user applications might not care about stream state, but relays will.
  virtual void OnStreamFin(const FullTrackName& full_track_name,
                           DataStreamIndex stream) = 0;
  virtual void OnStreamReset(const FullTrackName& full_track_name,
                             DataStreamIndex stream) = 0;
};

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

// Called when a PUBLISH message is received from the peer. Returns a visitor
// for the subscription. If the returned visitor is nullptr, the session will
// immediately reject the PUBLISH. Otherwise, it will deliver objects for the
// track until either MoqtResponseCallback returns with an error or the
// application calls Unsubscribe.
using MoqtIncomingPublishCallback = quiche::MultiUseCallback<SubscribeVisitor*(
    const FullTrackName&, const MessageParameters&, const TrackExtensions&,
    MoqtResponseCallback)>;

// Called whenever a PUBLISH_NAMESPACE or PUBLISH_NAMESPACE_DONE message is
// received from the peer. PUBLISH_NAMESPACE sets a pointer |parameters|,
// closing it does not. This callback is not invoked by NAMESPACE or
// NAMESPACE_DONE messages that arrive on a SUBSCRIBE_NAMESPACE stream.
// If the PUBLISH_NAMESPACE is updated, it will be called again, so be prepared
// for duplicates.
using MoqtIncomingPublishNamespaceCallback = quiche::MultiUseCallback<void(
    const TrackNamespace& track_namespace,
    const MessageParameters* absl_nullable parameters,
    MoqtResponseCallback callback)>;

// Called whenever SUBSCRIBE_NAMESPACE is received from the peer. Unsubscribe
// is signalled by destroying MoqtNamespaceTask.
// Calling MoqtNamespaceTask::SetObjectsAvailableCallback() will get all the
// tracks and namespaces, as appropriate, that are already present.
using MoqtIncomingSubscribeNamespaceCallback =
    quiche::MultiUseCallback<std::unique_ptr<MoqtNamespaceTask>(
        const TrackNamespace& prefix, const MessageParameters& parameters,
        MoqtResponseCallback response_callback)>;
using MoqtIncomingSubscribeTracksCallback = quiche::MultiUseCallback<void(
    const TrackNamespace& prefix, const MessageParameters& parameters,
    MoqtResponseCallback response_callback)>;

inline void DefaultIncomingPublishNamespaceCallback(
    const TrackNamespace&, const MessageParameters* absl_nullable,
    MoqtResponseCallback callback) {
  if (callback == nullptr) {
    return;
  }
  return std::move(callback)(MoqtRequestErrorInfo{
      RequestErrorCode::kNotSupported, std::nullopt,
      "This endpoint does not support incoming PUBLISH_NAMESPACE messages"});
};

inline std::unique_ptr<MoqtNamespaceTask>
DefaultIncomingSubscribeNamespaceCallback(
    const TrackNamespace&, const MessageParameters&,
    MoqtResponseCallback response_callback) {
  std::move(response_callback)(
      MoqtRequestErrorInfo{RequestErrorCode::kNotSupported, std::nullopt,
                           "This endpoint cannot publish."});
  return nullptr;
}

// If |response_callback| is nullptr, it's removing a subscription.
inline void DefaultIncomingSubscribeTracksCallback(
    const TrackNamespace&, const MessageParameters&,
    MoqtResponseCallback response_callback) {
  std::move(response_callback)(
      MoqtRequestErrorInfo{RequestErrorCode::kNotSupported, std::nullopt,
                           "This endpoint cannot publish."});
}

inline SubscribeVisitor* DefaultIncomingPublishCallback(
    const FullTrackName&, const MessageParameters&, const TrackExtensions&,
    MoqtResponseCallback) {
  return nullptr;
}

// Callbacks for session-level events.
struct MoqtSessionCallbacks {
  MoqtSessionEstablishedCallback session_established_callback = +[] {};
  MoqtSessionGoAwayCallback goaway_received_callback =
      +[](absl::string_view) {};
  MoqtSessionTerminatedCallback session_terminated_callback =
      +[](absl::string_view) {};
  MoqtSessionDeletedCallback session_deleted_callback = +[] {};

  MoqtIncomingPublishNamespaceCallback incoming_publish_namespace_callback =
      DefaultIncomingPublishNamespaceCallback;
  MoqtIncomingSubscribeNamespaceCallback incoming_subscribe_namespace_callback =
      DefaultIncomingSubscribeNamespaceCallback;
  MoqtIncomingSubscribeTracksCallback incoming_subscribe_tracks_callback =
      DefaultIncomingSubscribeTracksCallback;
  MoqtIncomingPublishCallback incoming_publish_callback =
      DefaultIncomingPublishCallback;
  const quic::QuicClock* clock = quic::QuicDefaultClock::Get();
};

}  // namespace moqt

#endif  // QUICHE_QUIC_MOQT_MOQT_SESSION_CALLBACKS_H_
