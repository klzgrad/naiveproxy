// Copyright (c) 2025 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "quiche/quic/moqt/tools/moqt_relay.h"

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <utility>

#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"
#include "quiche/quic/core/crypto/proof_source.h"
#include "quiche/quic/core/crypto/proof_verifier.h"
#include "quiche/quic/core/io/quic_event_loop.h"
#include "quiche/quic/core/quic_server_id.h"
#include "quiche/quic/moqt/moqt_messages.h"
#include "quiche/quic/moqt/moqt_session.h"
#include "quiche/quic/moqt/moqt_session_callbacks.h"
#include "quiche/quic/moqt/moqt_session_interface.h"
#include "quiche/quic/moqt/tools/moqt_client.h"
#include "quiche/quic/moqt/tools/moqt_server.h"
#include "quiche/quic/platform/api/quic_default_proof_providers.h"
#include "quiche/quic/platform/api/quic_socket_address.h"
#include "quiche/quic/tools/fake_proof_verifier.h"
#include "quiche/quic/tools/quic_name_lookup.h"
#include "quiche/quic/tools/quic_url.h"
#include "quiche/common/platform/api/quiche_logging.h"
#include "quiche/common/quiche_ip_address.h"

namespace moqt {

MoqtRelay::MoqtRelay(std::unique_ptr<quic::ProofSource> proof_source,
                     std::string bind_address, uint16_t bind_port,
                     absl::string_view default_upstream,
                     bool ignore_certificate)
    : MoqtRelay(std::move(proof_source), bind_address, bind_port,
                default_upstream, ignore_certificate, nullptr) {}

// protected members.
MoqtRelay::MoqtRelay(std::unique_ptr<quic::ProofSource> proof_source,
                     std::string bind_address, uint16_t bind_port,
                     absl::string_view default_upstream,
                     bool ignore_certificate,
                     quic::QuicEventLoop* client_event_loop)
    : ignore_certificate_(ignore_certificate),
      client_event_loop_(client_event_loop),
      // TODO(martinduke): Extend MoqtServer so that partial objects can be
      // received.
      server_(std::make_unique<MoqtServer>(std::move(proof_source),
                                           [this](absl::string_view path) {
                                             return IncomingSessionHandler(
                                                 path);
                                           })) {
  quiche::QuicheIpAddress bind_ip_address;
  QUICHE_CHECK(bind_ip_address.FromString(bind_address));
  // CreateUDPSocketAndListen() creates the event loop that we will pass to
  // MoqtClient.
  server_->quic_server().CreateUDPSocketAndListen(
      quic::QuicSocketAddress(bind_ip_address, bind_port));
  if (!default_upstream.empty()) {
    quic::QuicUrl url(default_upstream, "https");
    if (client_event_loop == nullptr) {
      client_event_loop = server_->quic_server().event_loop();
    }
    default_upstream_client_ =
        CreateClient(url, ignore_certificate, client_event_loop_);
    default_upstream_client_->Connect(url.PathParamsQuery(),
                                      CreateClientCallbacks());
  }
}

// private members.
std::unique_ptr<moqt::MoqtClient> MoqtRelay::CreateClient(
    quic::QuicUrl url, bool ignore_certificate,
    quic::QuicEventLoop* event_loop) {
  quic::QuicServerId server_id(url.host(), url.port());
  quic::QuicSocketAddress peer_address =
      quic::tools::LookupAddress(AF_UNSPEC, server_id);
  std::unique_ptr<quic::ProofVerifier> verifier;
  if (ignore_certificate) {
    verifier = std::make_unique<quic::FakeProofVerifier>();
  } else {
    verifier = quic::CreateDefaultProofVerifier(server_id.host());
  }
  return std::make_unique<moqt::MoqtClient>(peer_address, server_id,
                                            std::move(verifier), event_loop);
}

MoqtSessionCallbacks MoqtRelay::CreateClientCallbacks() {
  MoqtSessionCallbacks callbacks;
  callbacks.session_established_callback = [this]() {
    MoqtSession* session = default_upstream_client_->session();
    session->set_publisher(&publisher_);
    publisher_.SetDefaultUpstreamSession(session);
    SetNamespaceCallbacks(session);
  };
  callbacks.goaway_received_callback = [](absl::string_view new_session_uri) {
    QUICHE_LOG(INFO) << "GoAway received, new session uri = "
                     << new_session_uri;
    // There's no asynchronous means today to connect to a new URL.
    // Therefore, just ignore GOAWAY.
  };
  return callbacks;
}

void MoqtRelay::SetNamespaceCallbacks(MoqtSessionInterface* session) {
  session->callbacks().incoming_publish_namespace_callback =
      [this, session](
          const TrackNamespace& track_namespace,
          const std::optional<VersionSpecificParameters>& parameters,
          MoqtResponseCallback callback) {
        if (parameters.has_value()) {
          return publisher_.OnPublishNamespace(track_namespace, *parameters,
                                               session, std::move(callback));
        } else {
          return publisher_.OnPublishNamespaceDone(track_namespace, session);
        }
      };
  session->callbacks().incoming_subscribe_namespace_callback =
      [this, session](
          const TrackNamespace& track_namespace,
          const std::optional<VersionSpecificParameters>& parameters,
          MoqtResponseCallback callback) {
        if (parameters.has_value()) {
          publisher_.AddNamespaceSubscriber(track_namespace, session);
          std::move(callback)(std::nullopt);
        } else {
          publisher_.RemoveNamespaceSubscriber(track_namespace, session);
        }
      };
}

absl::StatusOr<MoqtConfigureSessionCallback> MoqtRelay::IncomingSessionHandler(
    absl::string_view /*path*/) {
  return [this](MoqtSession* session) {
    session->callbacks().session_established_callback = [this, session]() {
      session->set_publisher(&publisher_);
    };
    SetNamespaceCallbacks(session);
  };
}

}  // namespace moqt
