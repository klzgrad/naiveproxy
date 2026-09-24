// Copyright (c) 2019 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef QUICHE_QUIC_QBONE_QBONE_PACKET_EXCHANGER_H_
#define QUICHE_QUIC_QBONE_QBONE_PACKET_EXCHANGER_H_

#include <cstddef>
#include <vector>

#include "absl/base/nullability.h"
#include "absl/status/statusor.h"
#include "absl/time/time.h"
#include "absl/types/span.h"
#include "quiche/common/quiche_callbacks.h"

namespace quic {

// Handles reading and writing on the local network and exchange packets between
// the local network with a QBONE connection.
class QboneClientPacketExchanger {
 public:
  struct ReadResult {
    absl::Span<const std::byte> packet;
    absl::Duration latency;
  };

  struct WriteResult {
    absl::Span<const std::byte> packet;
    absl::Duration latency;
  };

  class Visitor {
   public:
    virtual ~Visitor() = default;

    virtual void OnRead(absl::StatusOr<std::vector<ReadResult>> results) = 0;
    virtual void OnWrite(absl::StatusOr<std::vector<WriteResult>> results) = 0;
  };

  virtual ~QboneClientPacketExchanger() = default;

  // Initializes the exchanger to allow read and write using the given file
  // descriptors.
  //
  // If `exchanger` is not null, it will be used instead of `this` to handle any
  // underlying-triggered read/write operations.
  virtual void Start(int read_fd, int write_fd,
                     QboneClientPacketExchanger* absl_nullable exchanger) = 0;

  // Uninitializes the exchanger and blocks until all pending read/write
  // operations (on- or off-thread) are complete. No Visitor callbacks will be
  // made after this completes.
  virtual void Stop() = 0;

  // Notifies the exchanger that at least one packet is ready to be read from
  // the network, and reads up to `max_packets_to_read`. Returns the number of
  // packets synchronously read from the socket (not number of valid packets
  // processed to client and visitor, and not useful if implementation handles
  // reads asynchronously). Must not be called before Start() or after Stop().
  virtual int OnReadFromNetworkReady(int max_packets_to_read) = 0;

  // Writes a packet to the local network. If the write would be blocked, the
  // packet is dropped. Must not be called before Start() or after Stop().
  virtual void WritePacketToNetwork(absl::Span<const std::byte> packet) = 0;
};

}  // namespace quic

#endif  // QUICHE_QUIC_QBONE_QBONE_PACKET_EXCHANGER_H_
