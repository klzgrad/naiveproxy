// Copyright 2026 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef QUICHE_QUIC_QBONE_MOCK_QBONE_PACKET_EXCHANGER_H_
#define QUICHE_QUIC_QBONE_MOCK_QBONE_PACKET_EXCHANGER_H_

#include <cstddef>
#include <vector>

#include "absl/base/nullability.h"
#include "absl/status/statusor.h"
#include "absl/types/span.h"
#include "quiche/quic/platform/api/quic_test.h"
#include "quiche/quic/qbone/bonnet/qbone_client_packet_exchanger.h"

namespace quic::test {

class MockQboneClientPacketExchanger : public QboneClientPacketExchanger {
 public:
  class MockVisitor : public QboneClientPacketExchanger::Visitor {
   public:
    MOCK_METHOD(
        void, OnRead,
        (absl::StatusOr<std::vector<QboneClientPacketExchanger::ReadResult>>),
        (override));
    MOCK_METHOD(
        void, OnWrite,
        (absl::StatusOr<std::vector<QboneClientPacketExchanger::WriteResult>>),
        (override));
  };

  MOCK_METHOD(void, Start,
              (int read_fd, int write_fd,
               QboneClientPacketExchanger* absl_nullable exchanger),
              (override));
  MOCK_METHOD(void, Stop, (), (override));
  MOCK_METHOD(int, OnReadFromNetworkReady, (int max_packets_to_read),
              (override));
  MOCK_METHOD(void, WritePacketToNetwork, (absl::Span<const std::byte> packet),
              (override));
};

}  // namespace quic::test

#endif  // QUICHE_QUIC_QBONE_MOCK_QBONE_PACKET_EXCHANGER_H_
