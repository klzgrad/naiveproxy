// Copyright 2025 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef QUICHE_QUIC_MOQT_MOQT_RELAY_PUBLISHER_H_
#define QUICHE_QUIC_MOQT_MOQT_RELAY_PUBLISHER_H_

#include <memory>

#include "absl/base/nullability.h"
#include "absl/container/flat_hash_map.h"
#include "quiche/quic/moqt/moqt_messages.h"
#include "quiche/quic/moqt/moqt_publisher.h"
#include "quiche/quic/moqt/moqt_relay_track_publisher.h"
#include "quiche/quic/moqt/moqt_session_callbacks.h"
#include "quiche/quic/moqt/moqt_session_interface.h"
#include "quiche/quic/moqt/relay_namespace_tree.h"
#include "quiche/common/quiche_weak_ptr.h"

namespace moqt {

// MoqtRelayPublisher is a publisher that connects sessions that request objects
// and namespaces with upstream sessions that can deliver those things.
class MoqtRelayPublisher : public MoqtPublisher {
 public:
  MoqtRelayPublisher() = default;
  MoqtRelayPublisher(const MoqtRelayPublisher&) = delete;
  MoqtRelayPublisher(MoqtRelayPublisher&&) = delete;
  MoqtRelayPublisher& operator=(const MoqtRelayPublisher&) = delete;
  MoqtRelayPublisher& operator=(MoqtRelayPublisher&&) = delete;

  // MoqtPublisher implementation.
  absl_nullable std::shared_ptr<MoqtTrackPublisher> GetTrack(
      const FullTrackName& track_name) override;

  void AddNamespaceSubscriber(const TrackNamespace& track_namespace,
                              MoqtSessionInterface* session) {
    namespace_publishers_.AddSubscriber(track_namespace, session);
  }
  void RemoveNamespaceSubscriber(const TrackNamespace& track_namespace,
                                 MoqtSessionInterface* session) {
    namespace_publishers_.RemoveSubscriber(track_namespace, session);
  }

  // There is a new default upstream session. When there is no other namespace
  // information, requests will route here.
  void SetDefaultUpstreamSession(
      MoqtSessionInterface* default_upstream_session);

  // Returns the default upstream session.
  quiche::QuicheWeakPtr<MoqtSessionInterface>& GetDefaultUpstreamSession() {
    return default_upstream_session_;
  }

  void OnPublishNamespace(const TrackNamespace& track_namespace,
                          const VersionSpecificParameters& parameters,
                          MoqtSessionInterface* session,
                          MoqtResponseCallback callback);

  void OnPublishNamespaceDone(const TrackNamespace& track_namespace,
                              MoqtSessionInterface* session);

 private:
  MoqtSessionInterface* GetUpstream(TrackNamespace& track_namespace);

  absl::flat_hash_map<FullTrackName, std::shared_ptr<MoqtRelayTrackPublisher>>
      tracks_;

  // An indexed map of namespace to a map of sessions. The key to the inner map
  // is indexed by a raw pointer, to make it easier to find entries when
  // deleting.
  RelayNamespaceTree namespace_publishers_;

  // TODO(martinduke): Add a map of Namespaces to namespace listeners.

  quiche::QuicheWeakPtr<MoqtSessionInterface> default_upstream_session_;
};

}  // namespace moqt

#endif  // QUICHE_QUIC_MOQT_MOQT_RELAY_PUBLISHER_H_
