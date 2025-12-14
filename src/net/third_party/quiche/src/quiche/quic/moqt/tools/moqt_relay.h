// Copyright (c) 2025 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef QUICHE_QUIC_MOQT_TOOLS_MOQT_RELAY_H_
#define QUICHE_QUIC_MOQT_TOOLS_MOQT_RELAY_H_

#include <cstdint>
#include <memory>
#include <string>

#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"
#include "quiche/quic/core/crypto/proof_source.h"
#include "quiche/quic/core/io/quic_event_loop.h"
#include "quiche/quic/moqt/moqt_relay_publisher.h"
#include "quiche/quic/moqt/moqt_session_callbacks.h"
#include "quiche/quic/moqt/moqt_session_interface.h"
#include "quiche/quic/moqt/tools/moqt_client.h"
#include "quiche/quic/moqt/tools/moqt_server.h"
#include "quiche/quic/tools/quic_url.h"

namespace moqt {

// Implements a pure MoqtRelay. It binds to |bind_address| and |bind_port| to
// listen for sessions, and optionally connects to |default_upstream| on
// startup that serves as a default route for requests.
// Requests for a track are forwarded to whatever session has published the
// relevant namespace, or the default route if not published.
// Incoming namespace subscriptions are stored locally.
// Incoming PUBLISH_NAMESPACE are forwarded to all adjacent sessions if
// broadcast_mode is true, otherwise only to sessions that have subscribed.
class MoqtRelay {
 public:
  // If |default_upstream| is empty, no default upstream session is created.
  MoqtRelay(std::unique_ptr<quic::ProofSource> proof_source,
            std::string bind_address, uint16_t bind_port,
            absl::string_view default_upstream, bool ignore_certificate);
  virtual ~MoqtRelay() = default;

  void HandleEventsForever() { server_->quic_server().HandleEventsForever(); }

 protected:  // Constructor for MoqtTestRelay.
  // If |client_event_loop| is null, the event loop from |server_| is used. For
  // test relays, it is not null, and the provided event loop is used for the
  // client. It will be the same event loop as the remote server, rather than
  // the local server.
  MoqtRelay(std::unique_ptr<quic::ProofSource> proof_source,
            std::string bind_address, uint16_t bind_port,
            absl::string_view default_upstream, bool ignore_certificate,
            quic::QuicEventLoop* client_event_loop);
  // Other functions for MoqtTestRelay.
  MoqtServer* server() { return server_.get(); }
  MoqtClient* client() { return default_upstream_client_.get(); }
  MoqtRelayPublisher* publisher() { return &publisher_; }

  virtual void SetNamespaceCallbacks(MoqtSessionInterface* session);

 private:
  std::unique_ptr<moqt::MoqtClient> CreateClient(
      quic::QuicUrl url, bool ignore_certificate,
      quic::QuicEventLoop* event_loop);

  MoqtSessionCallbacks CreateClientCallbacks();

  absl::StatusOr<MoqtConfigureSessionCallback> IncomingSessionHandler(
      absl::string_view path);

  const bool ignore_certificate_;
  quic::QuicEventLoop* client_event_loop_;

  MoqtRelayPublisher publisher_;

  // Pointer to a client that has received GOAWAY.
  std::unique_ptr<MoqtClient> default_upstream_client_;
  std::unique_ptr<MoqtServer> server_;
};

}  // namespace moqt

#endif  // QUICHE_QUIC_MOQT_TOOLS_MOQT_RELAY_H_
