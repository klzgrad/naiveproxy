// Copyright 2023 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "quiche/quic/moqt/tools/moqt_client.h"

#include <memory>
#include <string>
#include <utility>

#include "absl/status/status.h"
#include "absl/types/span.h"
#include "quiche/quic/core/crypto/proof_verifier.h"
#include "quiche/quic/core/io/quic_event_loop.h"
#include "quiche/quic/core/quic_server_id.h"
#include "quiche/quic/core/quic_types.h"
#include "quiche/quic/moqt/moqt_messages.h"
#include "quiche/quic/moqt/moqt_quic_config.h"
#include "quiche/quic/moqt/moqt_session.h"
#include "quiche/quic/moqt/moqt_session_callbacks.h"
#include "quiche/quic/moqt/moqt_session_interface.h"
#include "quiche/quic/platform/api/quic_socket_address.h"
#include "quiche/quic/tools/quic_name_lookup.h"
#include "quiche/quic/tools/web_transport_only_client.h"
#include "quiche/web_transport/web_transport.h"

namespace moqt {

MoqtClient::MoqtClient(quic::QuicSocketAddress peer_address,
                       const quic::QuicServerId& server_id,
                       std::unique_ptr<quic::ProofVerifier> proof_verifier,
                       quic::QuicEventLoop* event_loop,
                       MoqtSessionParameters parameters)
    : client_(peer_address, server_id, GetMoqtSupportedQuicVersions(),
              GenerateQuicConfig(), event_loop, nullptr,
              std::move(proof_verifier), nullptr),
      parameters_(parameters) {
  TuneQuicConfig(*client_.config());
  parameters_.perspective = quic::Perspective::IS_CLIENT;
}

void MoqtClient::Connect(std::string path, MoqtSessionCallbacks callbacks) {
  absl::Status status = ConnectInner(std::move(path), callbacks);
  if (!status.ok()) {
    std::move(callbacks.session_terminated_callback)(status.message());
  }
}

absl::Status MoqtClient::ConnectInner(std::string path,
                                      MoqtSessionCallbacks& callbacks) {
  const std::string version = std::string(kDefaultMoqtVersion);

  MoqtSessionDeletedCallback deleted_callback =
      std::move(callbacks.session_deleted_callback);
  callbacks.session_deleted_callback =
      [this, old = std::move(deleted_callback)]() mutable {
        session_ = nullptr;
        std::move(old)();
      };

  return client_.ConnectSync(
      path,
      [&](webtransport::Session* session) {
        auto moqt_session =
            std::make_unique<MoqtSession>(session, parameters_,
                                          client_.default_network_helper()
                                              ->event_loop()
                                              ->CreateAlarmFactory(),
                                          std::move(callbacks));
        session_ = moqt_session.get();
        return moqt_session;
      },
      absl::MakeSpan(&version, 1));
}

}  // namespace moqt
