// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "quiche/quic/tools/quic_server.h"

#include <cstdint>
#include <memory>

#include "quiche/quic/core/crypto/crypto_handshake.h"
#include "quiche/quic/core/crypto/quic_random.h"
#include "quiche/quic/core/io/event_loop_socket_factory.h"
#include "quiche/quic/core/io/quic_default_event_loop.h"
#include "quiche/quic/core/io/quic_event_loop.h"
#include "quiche/quic/core/quic_clock.h"
#include "quiche/quic/core/quic_crypto_stream.h"
#include "quiche/quic/core/quic_data_reader.h"
#include "quiche/quic/core/quic_default_clock.h"
#include "quiche/quic/core/quic_default_connection_helper.h"
#include "quiche/quic/core/quic_default_packet_writer.h"
#include "quiche/quic/core/quic_dispatcher.h"
#include "quiche/quic/core/quic_packet_reader.h"
#include "quiche/quic/core/quic_packets.h"
#include "quiche/quic/platform/api/quic_flags.h"
#include "quiche/quic/platform/api/quic_logging.h"
#include "quiche/quic/tools/quic_simple_crypto_server_stream_helper.h"
#include "quiche/quic/tools/quic_simple_dispatcher.h"
#include "quiche/quic/tools/quic_simple_server_backend.h"
#include "quiche/common/simple_buffer_allocator.h"

namespace quic {

namespace {

const char kSourceAddressTokenSecret[] = "secret";

}  // namespace

const size_t kNumSessionsToCreatePerSocketEvent = 16;

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
    : port_(0),
      fd_(-1),
      packets_dropped_(0),
      overflow_supported_(false),
      silent_close_(false),
      config_(config),
      crypto_config_(kSourceAddressTokenSecret, QuicRandom::GetInstance(),
                     std::move(proof_source), KeyExchangeSource::Default()),
      crypto_config_options_(crypto_config_options),
      version_manager_(supported_versions),
      packet_reader_(new QuicPacketReader()),
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
  close(fd_);
  fd_ = -1;

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

  QuicUdpSocketApi socket_api;
  fd_ = socket_api.Create(address.host().AddressFamilyToInt(),
                          /*receive_buffer_size =*/kDefaultSocketReceiveBuffer,
                          /*send_buffer_size =*/kDefaultSocketReceiveBuffer);
  if (fd_ == kQuicInvalidSocketFd) {
    QUIC_LOG(ERROR) << "CreateSocket() failed: " << strerror(errno);
    return false;
  }

  overflow_supported_ = socket_api.EnableDroppedPacketCount(fd_);
  socket_api.EnableReceiveTimestamp(fd_);

  bool success = socket_api.Bind(fd_, address);
  if (!success) {
    QUIC_LOG(ERROR) << "Bind failed: " << strerror(errno);
    return false;
  }
  QUIC_LOG(INFO) << "Listening on " << address.ToString();
  port_ = address.port();
  if (port_ == 0) {
    QuicSocketAddress address;
    if (address.FromSocket(fd_) != 0) {
      QUIC_LOG(ERROR) << "Unable to get self address.  Error: "
                      << strerror(errno);
    }
    port_ = address.port();
  }

  bool register_result = event_loop_->RegisterSocket(
      fd_, kSocketEventReadable | kSocketEventWritable, this);
  if (!register_result) {
    return false;
  }
  dispatcher_.reset(CreateQuicDispatcher());
  dispatcher_->InitializeWithWriter(CreateWriter(fd_));

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

  dispatcher_.reset();
  event_loop_.reset();
}

void QuicServer::OnSocketEvent(QuicEventLoop* /*event_loop*/,
                               QuicUdpSocketFd fd, QuicSocketEventMask events) {
  QUICHE_DCHECK_EQ(fd, fd_);

  if (events & kSocketEventReadable) {
    QUIC_DVLOG(1) << "EPOLLIN";

    dispatcher_->ProcessBufferedChlos(kNumSessionsToCreatePerSocketEvent);

    bool more_to_read = true;
    while (more_to_read) {
      more_to_read = packet_reader_->ReadAndDispatchPackets(
          fd_, port_, *QuicDefaultClock::Get(), dispatcher_.get(),
          overflow_supported_ ? &packets_dropped_ : nullptr);
    }

    if (dispatcher_->HasChlosBuffered()) {
      // Register EPOLLIN event to consume buffered CHLO(s).
      bool success =
          event_loop_->ArtificiallyNotifyEvent(fd_, kSocketEventReadable);
      QUICHE_DCHECK(success);
    }
    if (!event_loop_->SupportsEdgeTriggered()) {
      bool success = event_loop_->RearmSocket(fd_, kSocketEventReadable);
      QUICHE_DCHECK(success);
    }
  }
  if (events & kSocketEventWritable) {
    dispatcher_->OnCanWrite();
    if (!event_loop_->SupportsEdgeTriggered() &&
        dispatcher_->HasPendingWrites()) {
      bool success = event_loop_->RearmSocket(fd_, kSocketEventWritable);
      QUICHE_DCHECK(success);
    }
  }
}

}  // namespace quic
