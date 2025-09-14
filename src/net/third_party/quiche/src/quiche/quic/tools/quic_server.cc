// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "quiche/quic/tools/quic_server.h"

#include <cerrno>
#include <cstdint>
#include <cstring>
#include <memory>
#include <utility>

#include "absl/status/statusor.h"
#include "quiche/quic/core/crypto/crypto_handshake_message.h"
#include "quiche/quic/core/crypto/proof_source.h"
#include "quiche/quic/core/crypto/quic_crypto_server_config.h"
#include "quiche/quic/core/crypto/quic_random.h"
#include "quiche/quic/core/io/event_loop_socket_factory.h"
#include "quiche/quic/core/io/quic_default_event_loop.h"
#include "quiche/quic/core/io/quic_event_loop.h"
#include "quiche/quic/core/io/quic_server_io_harness.h"
#include "quiche/quic/core/io/socket.h"
#include "quiche/quic/core/quic_connection_id.h"
#include "quiche/quic/core/quic_constants.h"
#include "quiche/quic/core/quic_crypto_server_stream_base.h"
#include "quiche/quic/core/quic_default_clock.h"
#include "quiche/quic/core/quic_default_connection_helper.h"
#include "quiche/quic/core/quic_default_packet_writer.h"
#include "quiche/quic/core/quic_dispatcher.h"
#include "quiche/quic/core/quic_packet_writer.h"
#include "quiche/quic/core/quic_time.h"
#include "quiche/quic/core/quic_udp_socket.h"
#include "quiche/quic/core/quic_versions.h"
#include "quiche/quic/platform/api/quic_logging.h"
#include "quiche/quic/platform/api/quic_socket_address.h"
#include "quiche/quic/tools/quic_simple_crypto_server_stream_helper.h"
#include "quiche/quic/tools/quic_simple_dispatcher.h"
#include "quiche/quic/tools/quic_simple_server_backend.h"
#include "quiche/common/platform/api/quiche_logging.h"
#include "quiche/common/simple_buffer_allocator.h"

namespace quic {

namespace {

const char kSourceAddressTokenSecret[] = "secret";

}  // namespace

QuicServer::QuicServer(std::unique_ptr<ProofSource> proof_source,
                       QuicSimpleServerBackend* quic_simple_server_backend)
    : QuicServer(std::move(proof_source), quic_simple_server_backend,
                 AllSupportedVersions()) {}

QuicServer::QuicServer(std::unique_ptr<ProofSource> proof_source,
                       QuicSimpleServerBackend* quic_simple_server_backend,
                       const ParsedQuicVersionVector& supported_versions)
    : QuicServer(std::move(proof_source), QuicConfig(),
                 QuicCryptoServerConfig::ConfigOptions(), supported_versions,
                 quic_simple_server_backend, kQuicDefaultConnectionIdLength) {}

QuicServer::QuicServer(
    std::unique_ptr<ProofSource> proof_source, const QuicConfig& config,
    const QuicCryptoServerConfig::ConfigOptions& crypto_config_options,
    const ParsedQuicVersionVector& supported_versions,
    QuicSimpleServerBackend* quic_simple_server_backend,
    uint8_t expected_server_connection_id_length)
    : silent_close_(false),
      config_(config),
      crypto_config_(kSourceAddressTokenSecret, QuicRandom::GetInstance(),
                     std::move(proof_source), KeyExchangeSource::Default()),
      crypto_config_options_(crypto_config_options),
      version_manager_(supported_versions),
      quic_simple_server_backend_(quic_simple_server_backend),
      expected_server_connection_id_length_(
          expected_server_connection_id_length),
      connection_id_generator_(expected_server_connection_id_length) {
  QUICHE_DCHECK(quic_simple_server_backend_);
  Initialize();
}

void QuicServer::Initialize() {
  // If an initial flow control window has not explicitly been set, then use a
  // sensible value for a server: 1 MB for session, 64 KB for each stream.
  const uint32_t kInitialSessionFlowControlWindow = 1 * 1024 * 1024;  // 1 MB
  const uint32_t kInitialStreamFlowControlWindow = 64 * 1024;         // 64 KB
  if (config_.GetInitialStreamFlowControlWindowToSend() ==
      kDefaultFlowControlSendWindow) {
    config_.SetInitialStreamFlowControlWindowToSend(
        kInitialStreamFlowControlWindow);
  }
  if (config_.GetInitialSessionFlowControlWindowToSend() ==
      kDefaultFlowControlSendWindow) {
    config_.SetInitialSessionFlowControlWindowToSend(
        kInitialSessionFlowControlWindow);
  }

  std::unique_ptr<CryptoHandshakeMessage> scfg(crypto_config_.AddDefaultConfig(
      QuicRandom::GetInstance(), QuicDefaultClock::Get(),
      crypto_config_options_));
}

QuicServer::~QuicServer() {
  // Ensure the I/O harness is gone before closing the socket.
  io_.reset();

  (void)socket_api::Close(fd_);
  fd_ = kInvalidSocketFd;

  // Should be fine without because nothing should send requests to the backend
  // after `this` is destroyed, but for extra pointer safety, clear the socket
  // factory from the backend before the socket factory is destroyed.
  quic_simple_server_backend_->SetSocketFactory(nullptr);
}

bool QuicServer::CreateUDPSocketAndListen(const QuicSocketAddress& address) {
  event_loop_ = CreateEventLoop();

  socket_factory_ = std::make_unique<EventLoopSocketFactory>(
      event_loop_.get(), quiche::SimpleBufferAllocator::Get());
  quic_simple_server_backend_->SetSocketFactory(socket_factory_.get());

  dispatcher_.reset(CreateQuicDispatcher());

  absl::StatusOr<SocketFd> fd = CreateAndBindServerSocket(address);
  if (!fd.ok()) {
    QUIC_LOG(ERROR) << "Failed to create and bind socket: " << fd;
    return false;
  }
  fd_ = *fd;
  dispatcher_->InitializeWithWriter(CreateWriter(fd_));

  absl::StatusOr<std::unique_ptr<QuicServerIoHarness>> io =
      QuicServerIoHarness::Create(event_loop_.get(), dispatcher_.get(), fd_);
  if (!io.ok()) {
    QUICHE_LOG(ERROR) << "Failed to create I/O harness: " << io.status();
    return false;
  }
  io_ = *std::move(io);

  QUIC_LOG(INFO) << "Listening on " << io_->local_address();

  return true;
}

QuicPacketWriter* QuicServer::CreateWriter(int fd) {
  return new QuicDefaultPacketWriter(fd);
}

QuicDispatcher* QuicServer::CreateQuicDispatcher() {
  return new QuicSimpleDispatcher(
      &config_, &crypto_config_, &version_manager_,
      std::make_unique<QuicDefaultConnectionHelper>(),
      std::unique_ptr<QuicCryptoServerStreamBase::Helper>(
          new QuicSimpleCryptoServerStreamHelper()),
      event_loop_->CreateAlarmFactory(), quic_simple_server_backend_,
      expected_server_connection_id_length_, connection_id_generator_);
}

std::unique_ptr<QuicEventLoop> QuicServer::CreateEventLoop() {
  return GetDefaultEventLoop()->Create(QuicDefaultClock::Get());
}

void QuicServer::HandleEventsForever() {
  while (true) {
    WaitForEvents();
  }
}

void QuicServer::WaitForEvents() {
  event_loop_->RunEventLoopOnce(QuicTime::Delta::FromMilliseconds(50));
}

void QuicServer::Shutdown() {
  if (!silent_close_) {
    // Before we shut down the epoll server, give all active sessions a chance
    // to notify clients that they're closing.
    dispatcher_->Shutdown();
  }

  io_.reset();
  dispatcher_.reset();
  event_loop_.reset();
}

}  // namespace quic
