// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/tools/quic/quic_server.h"

#include <errno.h>
#include <features.h>
#include <netinet/in.h>
#include <string.h>
#include <sys/epoll.h>
#include <sys/socket.h>

#include <memory>

#include "net/quic/core/crypto/crypto_handshake.h"
#include "net/quic/core/crypto/quic_random.h"
#include "net/quic/core/quic_crypto_stream.h"
#include "net/quic/core/quic_data_reader.h"
#include "net/quic/core/quic_packets.h"
#include "net/quic/platform/api/quic_clock.h"
#include "net/quic/platform/api/quic_flags.h"
#include "net/quic/platform/api/quic_logging.h"
#include "net/tools/quic/platform/impl/quic_epoll_clock.h"
#include "net/tools/quic/platform/impl/quic_socket_utils.h"
#include "net/tools/quic/quic_dispatcher.h"
#include "net/tools/quic/quic_epoll_alarm_factory.h"
#include "net/tools/quic/quic_epoll_connection_helper.h"
#include "net/tools/quic/quic_http_response_cache.h"
#include "net/tools/quic/quic_packet_reader.h"
#include "net/tools/quic/quic_simple_crypto_server_stream_helper.h"
#include "net/tools/quic/quic_simple_dispatcher.h"

#ifndef SO_RXQ_OVFL
#define SO_RXQ_OVFL 40
#endif

namespace net {

namespace {

// Specifies the directory used during QuicHttpResponseCache
// construction to seed the cache. Cache directory can be
// generated using `wget -p --save-headers <url>`
std::string FLAGS_quic_response_cache_dir = "";

const int kEpollFlags = EPOLLIN | EPOLLOUT | EPOLLET;
const char kSourceAddressTokenSecret[] = "secret";

}  // namespace

const size_t kNumSessionsToCreatePerSocketEvent = 16;

QuicServer::QuicServer(std::unique_ptr<ProofSource> proof_source,
                       QuicHttpResponseCache* response_cache)
    : QuicServer(std::move(proof_source),
                 QuicConfig(),
                 QuicCryptoServerConfig::ConfigOptions(),
                 AllSupportedTransportVersions(),
                 response_cache) {}

QuicServer::QuicServer(
    std::unique_ptr<ProofSource> proof_source,
    const QuicConfig& config,
    const QuicCryptoServerConfig::ConfigOptions& crypto_config_options,
    const QuicTransportVersionVector& supported_versions,
    QuicHttpResponseCache* response_cache)
    : port_(0),
      fd_(-1),
      packets_dropped_(0),
      overflow_supported_(false),
      silent_close_(false),
      config_(config),
      crypto_config_(kSourceAddressTokenSecret,
                     QuicRandom::GetInstance(),
                     std::move(proof_source)),
      crypto_config_options_(crypto_config_options),
      version_manager_(supported_versions),
      packet_reader_(new QuicPacketReader()),
      response_cache_(response_cache) {
  Initialize();
}

void QuicServer::Initialize() {
  // If an initial flow control window has not explicitly been set, then use a
  // sensible value for a server: 1 MB for session, 64 KB for each stream.
  const uint32_t kInitialSessionFlowControlWindow = 1 * 1024 * 1024;  // 1 MB
  const uint32_t kInitialStreamFlowControlWindow = 64 * 1024;         // 64 KB
  if (config_.GetInitialStreamFlowControlWindowToSend() ==
      kMinimumFlowControlSendWindow) {
    config_.SetInitialStreamFlowControlWindowToSend(
        kInitialStreamFlowControlWindow);
  }
  if (config_.GetInitialSessionFlowControlWindowToSend() ==
      kMinimumFlowControlSendWindow) {
    config_.SetInitialSessionFlowControlWindowToSend(
        kInitialSessionFlowControlWindow);
  }

  epoll_server_.set_timeout_in_us(50 * 1000);

  if (!FLAGS_quic_response_cache_dir.empty()) {
    response_cache_->InitializeFromDirectory(FLAGS_quic_response_cache_dir);
  }

  QuicEpollClock clock(&epoll_server_);

  std::unique_ptr<CryptoHandshakeMessage> scfg(crypto_config_.AddDefaultConfig(
      QuicRandom::GetInstance(), &clock, crypto_config_options_));
}

QuicServer::~QuicServer() {}

bool QuicServer::CreateUDPSocketAndListen(const QuicSocketAddress& address) {
  fd_ = QuicSocketUtils::CreateUDPSocket(address, &overflow_supported_);
  if (fd_ < 0) {
    QUIC_LOG(ERROR) << "CreateSocket() failed: " << strerror(errno);
    return false;
  }

  sockaddr_storage addr = address.generic_address();
  int rc = bind(fd_, reinterpret_cast<sockaddr*>(&addr), sizeof(addr));
  if (rc < 0) {
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

  epoll_server_.RegisterFD(fd_, this, kEpollFlags);
  dispatcher_.reset(CreateQuicDispatcher());
  dispatcher_->InitializeWithWriter(CreateWriter(fd_));

  return true;
}

QuicDefaultPacketWriter* QuicServer::CreateWriter(int fd) {
  return new QuicDefaultPacketWriter(fd);
}

QuicDispatcher* QuicServer::CreateQuicDispatcher() {
  QuicEpollAlarmFactory alarm_factory(&epoll_server_);
  return new QuicSimpleDispatcher(
      config_, &crypto_config_, &version_manager_,
      std::unique_ptr<QuicEpollConnectionHelper>(new QuicEpollConnectionHelper(
          &epoll_server_, QuicAllocator::BUFFER_POOL)),
      std::unique_ptr<QuicCryptoServerStream::Helper>(
          new QuicSimpleCryptoServerStreamHelper(QuicRandom::GetInstance())),
      std::unique_ptr<QuicEpollAlarmFactory>(
          new QuicEpollAlarmFactory(&epoll_server_)),
      response_cache_);
}

void QuicServer::WaitForEvents() {
  epoll_server_.WaitForEventsAndExecuteCallbacks();
}

void QuicServer::Shutdown() {
  if (!silent_close_) {
    // Before we shut down the epoll server, give all active sessions a chance
    // to notify clients that they're closing.
    dispatcher_->Shutdown();
  }

  close(fd_);
  fd_ = -1;
}

void QuicServer::OnEvent(int fd, EpollEvent* event) {
  DCHECK_EQ(fd, fd_);
  event->out_ready_mask = 0;

  if (event->in_events & EPOLLIN) {
    QUIC_DVLOG(1) << "EPOLLIN";

    dispatcher_->ProcessBufferedChlos(kNumSessionsToCreatePerSocketEvent);

    bool more_to_read = true;
    while (more_to_read) {
      more_to_read = packet_reader_->ReadAndDispatchPackets(
          fd_, port_, QuicEpollClock(&epoll_server_), dispatcher_.get(),
          overflow_supported_ ? &packets_dropped_ : nullptr);
    }

    if (dispatcher_->HasChlosBuffered()) {
      // Register EPOLLIN event to consume buffered CHLO(s).
      event->out_ready_mask |= EPOLLIN;
    }
  }
  if (event->in_events & EPOLLOUT) {
    dispatcher_->OnCanWrite();
    if (dispatcher_->HasPendingWrites()) {
      event->out_ready_mask |= EPOLLOUT;
    }
  }
  if (event->in_events & EPOLLERR) {
  }
}

}  // namespace net
