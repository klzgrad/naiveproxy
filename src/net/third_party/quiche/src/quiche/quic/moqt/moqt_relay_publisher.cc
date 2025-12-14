// Copyright 2025 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "quiche/quic/moqt/moqt_relay_publisher.h"

#include <memory>
#include <optional>
#include <utility>

#include "absl/base/nullability.h"
#include "absl/container/flat_hash_map.h"
#include "absl/strings/string_view.h"
#include "quiche/quic/moqt/moqt_messages.h"
#include "quiche/quic/moqt/moqt_publisher.h"
#include "quiche/quic/moqt/moqt_relay_track_publisher.h"
#include "quiche/quic/moqt/moqt_session_callbacks.h"
#include "quiche/quic/moqt/moqt_session_interface.h"
#include "quiche/common/quiche_weak_ptr.h"

namespace moqt {

using quiche::QuicheWeakPtr;

absl_nullable std::shared_ptr<MoqtTrackPublisher> MoqtRelayPublisher::GetTrack(
    const FullTrackName& track_name) {
  auto it = tracks_.find(track_name);
  if (it != tracks_.end()) {
    return it->second;
  }
  // Make a copy, because this namespace might be truncated.
  TrackNamespace track_namespace = track_name.track_namespace();
  MoqtSessionInterface* upstream = GetUpstream(track_namespace);
  if (upstream == nullptr) {
    return nullptr;
  }
  auto track_publisher = std::make_shared<MoqtRelayTrackPublisher>(
      track_name, upstream->GetWeakPtr(),
      [this, track_name] { tracks_.erase(track_name); }, std::nullopt,
      std::nullopt);
  tracks_[track_name] = track_publisher;
  return track_publisher;
}

void MoqtRelayPublisher::SetDefaultUpstreamSession(
    MoqtSessionInterface* default_upstream_session) {
  MoqtSessionInterface* old_session =
      default_upstream_session_.GetIfAvailable();
  if (old_session != nullptr) {
    // The Publisher no longer cares if the old session is terminated.
    old_session->callbacks().session_terminated_callback =
        [](absl::string_view) {};
  }
  // Update callbacks.
  // goaway_received_callback has already been set by MoqtClient. It will
  // handle connecting to new URI and calling AddDefaultUpstreamSession() again
  // when that session is ready.
  default_upstream_session->callbacks().session_terminated_callback =
      [this](absl::string_view error_message) {
        QUICHE_LOG(INFO) << "Default upstream session terminated, error = "
                         << error_message;
        default_upstream_session_ = QuicheWeakPtr<MoqtSessionInterface>();
      };
  default_upstream_session_ = default_upstream_session->GetWeakPtr();
}

void MoqtRelayPublisher::OnPublishNamespace(
    const TrackNamespace& track_namespace,
    const VersionSpecificParameters& /*parameters*/,
    MoqtSessionInterface* session, MoqtResponseCallback callback) {
  if (session == nullptr) {
    return;
  }
  // TODO(martinduke): Handle parameters.
  namespace_publishers_.AddPublisher(track_namespace, session);
  // TODO(martinduke): Notify subscribers listening for this namespace.
  // Send PUBLISH_NAMESPACE_OK.
  std::move(callback)(std::nullopt);
}

void MoqtRelayPublisher::OnPublishNamespaceDone(
    const TrackNamespace& track_namespace, MoqtSessionInterface* session) {
  if (session == nullptr) {
    return;
  }
  namespace_publishers_.RemovePublisher(track_namespace, session);
  // TODO(martinduke): Notify subscribers listening for this namespace.
}

MoqtSessionInterface* MoqtRelayPublisher::GetUpstream(
    TrackNamespace& track_namespace) {
  MoqtSessionInterface* upstream =
      namespace_publishers_.GetValidPublisher(track_namespace);
  return (upstream == nullptr) ? default_upstream_session_.GetIfAvailable()
                               : upstream;
}

}  // namespace moqt
