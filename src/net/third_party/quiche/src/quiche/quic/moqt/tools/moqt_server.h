// Copyright 2023 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef QUICHE_QUIC_MOQT_TOOLS_MOQT_SERVER_H_

#define QUICHE_QUIC_MOQT_TOOLS_MOQT_SERVER_H_

#include <memory>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"
#include "quiche/quic/core/crypto/proof_source.h"
#include "quiche/quic/core/crypto/quic_crypto_server_config.h"
#include "quiche/quic/core/deterministic_connection_id_generator.h"
#include "quiche/quic/core/http/web_transport_only_dispatcher.h"
#include "quiche/quic/core/io/quic_event_loop.h"
#include "quiche/quic/core/io/quic_server_io_harness.h"
#include "quiche/quic/core/io/socket.h"
#include "quiche/quic/core/quic_config.h"
#include "quiche/quic/core/quic_version_manager.h"
#include "quiche/quic/moqt/moqt_session.h"
#include "quiche/quic/platform/api/quic_socket_address.h"
#include "quiche/common/platform/api/quiche_export.h"
#include "quiche/common/quiche_callbacks.h"

namespace moqt {

namespace test {
class MoqtServerPeer;
}  // namespace test

// A callback to configure an already created MoQT session.
using MoqtConfigureSessionCallback =
    quiche::SingleUseCallback<void(MoqtSession* session)>;

// A callback to provide MoQT handler based on the path in the request.
using MoqtIncomingSessionCallback =
    quiche::MultiUseCallback<absl::StatusOr<MoqtConfigureSessionCallback>(
        absl::string_view path)>;

// A simple MoQT server.
class QUICHE_EXPORT MoqtServer {
 public:
  explicit MoqtServer(std::unique_ptr<quic::ProofSource> proof_source,
                      MoqtIncomingSessionCallback callback);

  MoqtServer(const MoqtServer&) = delete;
  MoqtServer(MoqtServer&&) = delete;
  MoqtServer& operator=(const MoqtServer&) = delete;
  MoqtServer& operator=(MoqtServer&&) = delete;

  absl::Status CreateUDPSocketAndListen(const quic::QuicSocketAddress& address);
  void WaitForEvents();
  void HandleEventsForever();
  quic::QuicEventLoop* event_loop() { return event_loop_.get(); }
  int port() { return io_->local_address().port(); }

 private:
  friend class test::MoqtServerPeer;
  quic::QuicConfig config_;
  quic::QuicCryptoServerConfig crypto_config_;
  quic::QuicVersionManager version_manager_;
  quic::DeterministicConnectionIdGenerator connection_id_generator_;
  std::unique_ptr<quic::QuicEventLoop> event_loop_;
  quic::WebTransportOnlyDispatcher dispatcher_;

  quic::OwnedSocketFd fd_;
  std::unique_ptr<quic::QuicServerIoHarness> io_;
};

}  // namespace moqt

#endif  // QUICHE_QUIC_MOQT_TOOLS_MOQT_SERVER_H_
