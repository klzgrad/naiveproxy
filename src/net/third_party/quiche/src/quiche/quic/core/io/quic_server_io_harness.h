// Copyright 2024 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef QUICHE_QUIC_CORE_IO_QUIC_SERVER_IO_HARNESS_H_
#define QUICHE_QUIC_CORE_IO_QUIC_SERVER_IO_HARNESS_H_

#include <cstddef>
#include <memory>
#include <utility>

#include "absl/base/nullability.h"
#include "absl/status/statusor.h"
#include "quiche/quic/core/io/quic_event_loop.h"
#include "quiche/quic/core/io/socket.h"
#include "quiche/quic/core/quic_dispatcher.h"
#include "quiche/quic/core/quic_packet_reader.h"
#include "quiche/quic/core/quic_types.h"
#include "quiche/quic/platform/api/quic_socket_address.h"

namespace quic {

// Creates a UDP socket and binds it to the specified address.
absl::StatusOr<SocketFd> CreateAndBindServerSocket(
    const QuicSocketAddress& bind_address);

// QuicServerIoHarness registers itself with the provided event loop, reads
// packets from the socket when the sockets becomes readable, and notifies the
// dispatcher whenever it becomes writable.
class QuicServerIoHarness : public QuicSocketEventListener {
 public:
  // Creates an I/O harness for the specified dispatcher and the file
  // descriptor.  Does not create a packet writer; the caller has to either call
  // InitializeWriter() manually, or provide their own writer.
  static absl::StatusOr<std::unique_ptr<QuicServerIoHarness>> Create(
      QuicEventLoop* /*absl_nonnull*/ event_loop,
      QuicDispatcher* /*absl_nonnull*/ dispatcher, SocketFd fd);

  ~QuicServerIoHarness();
  QuicServerIoHarness(const QuicServerIoHarness&) = delete;
  QuicServerIoHarness(QuicServerIoHarness&&) = delete;
  QuicServerIoHarness& operator=(const QuicServerIoHarness&) = delete;
  QuicServerIoHarness& operator=(QuicServerIoHarness&&) = delete;

  // Initializes the dispatcher with a default packet writer.
  void InitializeWriter();

  SocketFd fd() const { return fd_; }
  QuicSocketAddress local_address() const { return local_address_; }
  QuicPacketCount packets_dropped() const { return packets_dropped_; }
  bool overflow_supported() const { return overflow_supported_; }

  void set_max_sessions_to_create_per_socket_event(size_t value) {
    max_sessions_to_create_per_socket_event_ = value;
  }
  void OverridePacketReaderForTests(std::unique_ptr<QuicPacketReader> reader) {
    reader_ = std::move(reader);
  }

  // QuicSocketEventListener implementation.
  void OnSocketEvent(QuicEventLoop* event_loop, SocketFd fd,
                     QuicSocketEventMask events) override;

 private:
  // Limits the maximum number of QUIC session objects that will be created per
  // a single iteration of the event loop.
  static constexpr size_t kNumSessionsToCreatePerSocketEvent = 16;

  QuicServerIoHarness(QuicEventLoop* event_loop, QuicDispatcher* dispatcher,
                      SocketFd fd);

  QuicEventLoop& event_loop_;
  QuicDispatcher& dispatcher_;
  const SocketFd fd_;

  QuicSocketAddress local_address_;
  std::unique_ptr<QuicPacketReader> reader_;
  QuicPacketCount packets_dropped_ = 0;
  bool overflow_supported_ = false;
  size_t max_sessions_to_create_per_socket_event_ =
      kNumSessionsToCreatePerSocketEvent;
};

}  // namespace quic

#endif  // QUICHE_QUIC_CORE_IO_QUIC_SERVER_IO_HARNESS_H_
