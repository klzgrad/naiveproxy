// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef QUICHE_QUIC_TEST_TOOLS_MOCK_CONNECTION_ID_GENERATOR_H_
#define QUICHE_QUIC_TEST_TOOLS_MOCK_CONNECTION_ID_GENERATOR_H_

#include "quiche/quic/core/connection_id_generator.h"
#include "quiche/quic/platform/api/quic_test.h"

namespace quic {
namespace test {

class MockConnectionIdGenerator : public quic::ConnectionIdGeneratorInterface {
 public:
  MOCK_METHOD(absl::optional<quic::QuicConnectionId>, GenerateNextConnectionId,
              (const quic::QuicConnectionId& original), (override));

  MOCK_METHOD(absl::optional<quic::QuicConnectionId>, MaybeReplaceConnectionId,
              (const quic::QuicConnectionId& original,
               const quic::ParsedQuicVersion& version),
              (override));

  MOCK_METHOD(uint8_t, ConnectionIdLength, (uint8_t first_byte),
              (const, override));
};

}  // namespace test
}  // namespace quic

#endif  // QUICHE_QUIC_TEST_TOOLS_MOCK_CONNECTION_ID_GENERATOR_H_
