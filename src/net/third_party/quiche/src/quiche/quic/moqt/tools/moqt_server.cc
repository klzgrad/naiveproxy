// Copyright 2023 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "quiche/quic/moqt/tools/moqt_server.h"

#include <cstddef>
#include <memory>
#include <string>
#include <utility>

#include "absl/algorithm/container.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"
#include "absl/types/span.h"
#include "quiche/quic/core/crypto/proof_source.h"
#include "quiche/quic/core/crypto/quic_crypto_server_config.h"
#include "quiche/quic/core/crypto/quic_random.h"
#include "quiche/quic/core/http/web_transport_only_server_session.h"
#include "quiche/quic/core/io/quic_default_event_loop.h"
#include "quiche/quic/core/io/quic_event_loop.h"
#include "quiche/quic/core/io/quic_server_io_harness.h"
#include "quiche/quic/core/quic_connection_id.h"
#include "quiche/quic/core/quic_default_clock.h"
#include "quiche/quic/core/quic_default_connection_helper.h"
#include "quiche/quic/core/quic_time.h"
#include "quiche/quic/core/quic_types.h"
#include "quiche/quic/core/quic_versions.h"
#include "quiche/quic/moqt/moqt_messages.h"
#include "quiche/quic/moqt/moqt_quic_config.h"
#include "quiche/quic/moqt/moqt_session.h"
#include "quiche/quic/platform/api/quic_socket_address.h"
#include "quiche/quic/tools/quic_simple_crypto_server_stream_helper.h"
#include "quiche/common/quiche_status_utils.h"
#include "quiche/web_transport/web_transport.h"

namespace moqt {

namespace {

std::string GenerateRandomTokenSecret() {
  constexpr size_t kSize = 256 / 8;
  char secret[kSize];
  quic::QuicRandom::GetInstance()->RandBytes(secret, sizeof(secret));
  return std::string(secret, sizeof(secret));
}

quic::WebTransportHandlerFactoryCallback CreateWebTransportCallback(
    MoqtIncomingSessionCallback callback, quic::QuicEventLoop* event_loop) {
  return [event_loop = event_loop, callback = std::move(callback)](
             webtransport::Session* session,
             const quic::WebTransportIncomingRequestDetails& details)
             -> absl::StatusOr<quic::WebTransportConnectResponse> {
    auto path_it = details.headers.find(":path");
    absl::StatusOr<MoqtConfigureSessionCallback> configurator =
        callback(path_it != details.headers.end() ? path_it->second : "");
    if (!configurator.ok()) {
      return configurator.status();
    }

    MoqtSessionParameters parameters(quic::Perspective::IS_SERVER);
    auto moqt_session = std::make_unique<MoqtSession>(
        session, parameters, event_loop->CreateAlarmFactory());
    std::move (*configurator)(moqt_session.get());

    quic::WebTransportConnectResponse response;
    response.visitor = std::move(moqt_session);
    return response;
  };
}
}  // namespace

MoqtServer::MoqtServer(std::unique_ptr<quic::ProofSource> proof_source,
                       MoqtIncomingSessionCallback callback)
    : config_(GenerateQuicConfig()),
      crypto_config_(GenerateRandomTokenSecret(),
                     quic::QuicRandom::GetInstance(), std::move(proof_source),
                     quic::KeyExchangeSource::Default()),
      version_manager_(quic::CurrentSupportedVersionsWithTls()),
      connection_id_generator_(quic::kQuicDefaultConnectionIdLength),
      event_loop_(
          quic::GetDefaultEventLoop()->Create(quic::QuicDefaultClock::Get())),
      dispatcher_(&config_, &crypto_config_, &version_manager_,
                  std::make_unique<quic::QuicDefaultConnectionHelper>(),
                  std::make_unique<quic::QuicSimpleCryptoServerStreamHelper>(),
                  event_loop_->CreateAlarmFactory(),
                  quic::kQuicDefaultConnectionIdLength,
                  connection_id_generator_) {
  dispatcher_.parameters().handler_factory =
      CreateWebTransportCallback(std::move(callback), event_loop_.get());
  dispatcher_.parameters().subprotocol_callback =
      +[](absl::Span<const absl::string_view> subprotocols) {
        return absl::c_find(subprotocols, kDefaultMoqtVersion) -
               subprotocols.begin();
      };
}

absl::Status MoqtServer::CreateUDPSocketAndListen(
    const quic::QuicSocketAddress& address) {
  QUICHE_ASSIGN_OR_RETURN(fd_, quic::CreateAndBindServerSocket(address));
  QUICHE_ASSIGN_OR_RETURN(io_, quic::QuicServerIoHarness::Create(
                                   event_loop_.get(), &dispatcher_, *fd_));
  io_->InitializeWriter();
  return absl::OkStatus();
}

void MoqtServer::WaitForEvents() {
  event_loop_->RunEventLoopOnce(quic::QuicTime::Delta::FromMilliseconds(50));
}

void MoqtServer::HandleEventsForever() {
  while (true) {
    WaitForEvents();
  }
}

}  // namespace moqt
