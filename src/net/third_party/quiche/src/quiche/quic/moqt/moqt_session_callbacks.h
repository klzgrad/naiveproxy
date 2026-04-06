// Copyright 2023 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef QUICHE_QUIC_MOQT_MOQT_SESSION_CALLBACKS_H_
#define QUICHE_QUIC_MOQT_MOQT_SESSION_CALLBACKS_H_

#include <memory>
#include <optional>
#include <utility>

#include "absl/strings/string_view.h"
#include "quiche/quic/core/quic_clock.h"
#include "quiche/quic/core/quic_default_clock.h"
#include "quiche/quic/moqt/moqt_error.h"
#include "quiche/quic/moqt/moqt_fetch_task.h"
#include "quiche/quic/moqt/moqt_key_value_pair.h"
#include "quiche/quic/moqt/moqt_messages.h"
#include "quiche/quic/moqt/moqt_names.h"
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

// Called whenever a PUBLISH_NAMESPACE or PUBLISH_NAMESPACE_DONE message is
// received from the peer. PUBLISH_NAMESPACE sets a value for |parameters|,
// PUBLISH_NAMESPACE_DONE does not. This callback is not invoked by NAMESPACE or
// NAMESPACE_DONE messages that arrive on a SUBSCRIBE_NAMESPACE stream.
// If the PUBLISH_NAMESPACE is updated, it will be called again, so be prepared
// for duplicates.
using MoqtIncomingPublishNamespaceCallback = quiche::MultiUseCallback<void(
    const TrackNamespace& track_namespace,
    const std::optional<MessageParameters>& parameters,
    MoqtResponseCallback callback)>;

// Called whenever SUBSCRIBE_NAMESPACE is received from the peer. Unsubscribe
// is signalled by destroying MoqtNamespaceTask.
// Calling MoqtNamespaceTask::SetObjectsAvailableCallback() will get all the
// tracks and namespaces, as appropriate, that are already present.
using MoqtIncomingSubscribeNamespaceCallback =
    quiche::MultiUseCallback<std::unique_ptr<MoqtNamespaceTask>(
        const TrackNamespace& prefix, SubscribeNamespaceOption option,
        const MessageParameters& parameters,
        MoqtResponseCallback response_callback)>;

inline void DefaultIncomingPublishNamespaceCallback(
    const TrackNamespace&, const std::optional<MessageParameters>&,
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
    const TrackNamespace&, SubscribeNamespaceOption, const MessageParameters&,
    MoqtResponseCallback response_callback) {
  std::move(response_callback)(
      MoqtRequestErrorInfo{RequestErrorCode::kNotSupported, std::nullopt,
                           "This endpoint cannot publish."});
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
  const quic::QuicClock* clock = quic::QuicDefaultClock::Get();
};

}  // namespace moqt

#endif  // QUICHE_QUIC_MOQT_MOQT_SESSION_CALLBACKS_H_
