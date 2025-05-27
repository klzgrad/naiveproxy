// Copyright 2024 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "quiche/quic/core/io/quic_server_io_harness.h"

#include <memory>

#include "absl/base/nullability.h"
#include "absl/memory/memory.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "quiche/quic/core/io/quic_event_loop.h"
#include "quiche/quic/core/io/socket.h"
#include "quiche/quic/core/quic_constants.h"
#include "quiche/quic/core/quic_default_clock.h"
#include "quiche/quic/core/quic_default_packet_writer.h"
#include "quiche/quic/core/quic_dispatcher.h"
#include "quiche/quic/core/quic_packet_reader.h"
#include "quiche/quic/core/quic_udp_socket.h"
#include "quiche/quic/platform/api/quic_socket_address.h"
#include "quiche/common/platform/api/quiche_logging.h"
#include "quiche/common/quiche_status_utils.h"

namespace quic {

absl::StatusOr<SocketFd> CreateAndBindServerSocket(
    const QuicSocketAddress& bind_address) {
  SocketFd fd = QuicUdpSocketApi().Create(
      bind_address.host().AddressFamilyToInt(),
      /*receive_buffer_size=*/kDefaultSocketReceiveBuffer,
      /*send_buffer_size=*/kDefaultSocketReceiveBuffer);
  if (fd == kQuicInvalidSocketFd) {
    return absl::InternalError("Failed to create socket");
  }

  bool success = QuicUdpSocketApi().Bind(fd, bind_address);
  if (!success) {
    (void)socket_api::Close(fd);
    return socket_api::GetSocketError(fd);
  }

  return fd;
}

absl::StatusOr<std::unique_ptr<QuicServerIoHarness>>
QuicServerIoHarness::Create(QuicEventLoop* /*absl_nonnull*/ event_loop,
                            QuicDispatcher* /*absl_nonnull*/ dispatcher,
                            SocketFd fd) {
  auto harness =
      absl::WrapUnique(new QuicServerIoHarness(event_loop, dispatcher, fd));

  absl::StatusOr<QuicSocketAddress> address = socket_api::GetSocketAddress(fd);
  QUICHE_RETURN_IF_ERROR(address.status());
  harness->local_address_ = *address;

  harness->overflow_supported_ =
      QuicUdpSocketApi().EnableDroppedPacketCount(fd);
  QuicUdpSocketApi().EnableReceiveTimestamp(fd);

  bool register_result = event_loop->RegisterSocket(
      fd, kSocketEventReadable | kSocketEventWritable, harness.get());
  if (!register_result) {
    return absl::InternalError(
        "Failed to register the socket with the I/O loop");
  }
  return harness;
}

QuicServerIoHarness::QuicServerIoHarness(QuicEventLoop* event_loop,
                                         QuicDispatcher* dispatcher,
                                         SocketFd fd)
    : event_loop_(*event_loop),
      dispatcher_(*dispatcher),
      fd_(fd),
      reader_(std::make_unique<QuicPacketReader>()) {
  QUICHE_DCHECK_NE(fd_, kInvalidSocketFd);
}

QuicServerIoHarness::~QuicServerIoHarness() {
  if (!event_loop_.UnregisterSocket(fd_)) {
    QUICHE_LOG(ERROR) << "Failed to unregister socket: " << fd_;
  }
}

void QuicServerIoHarness::InitializeWriter() {
  dispatcher_.InitializeWithWriter(new QuicDefaultPacketWriter(fd_));
}

void QuicServerIoHarness::OnSocketEvent(QuicEventLoop* /*event_loop*/,
                                        SocketFd fd,
                                        QuicSocketEventMask events) {
  QUICHE_DCHECK_EQ(fd, fd_);

  if (events & kSocketEventReadable) {
    QUICHE_DVLOG(1) << "EPOLLIN";

    dispatcher_.ProcessBufferedChlos(max_sessions_to_create_per_socket_event_);

    bool more_to_read = true;
    while (more_to_read) {
      more_to_read = reader_->ReadAndDispatchPackets(
          fd_, local_address_.port(), *QuicDefaultClock::Get(), &dispatcher_,
          overflow_supported_ ? &packets_dropped_ : nullptr);
    }

    if (dispatcher_.HasChlosBuffered()) {
      // Register EPOLLIN event to consume buffered CHLO(s).
      bool success =
          event_loop_.ArtificiallyNotifyEvent(fd_, kSocketEventReadable);
      QUICHE_DCHECK(success);
    }
    if (!event_loop_.SupportsEdgeTriggered()) {
      bool success = event_loop_.RearmSocket(fd_, kSocketEventReadable);
      QUICHE_DCHECK(success);
    }
  }
  if (events & kSocketEventWritable) {
    dispatcher_.OnCanWrite();
    if (!event_loop_.SupportsEdgeTriggered() &&
        dispatcher_.HasPendingWrites()) {
      bool success = event_loop_.RearmSocket(fd_, kSocketEventWritable);
      QUICHE_DCHECK(success);
    }
  }
}

}  // namespace quic
