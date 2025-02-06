// Copyright (c) 2023 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef QUICHE_QUIC_TEST_TOOLS_MOCK_LOAD_BALANCER_ENCODER_H_
#define QUICHE_QUIC_TEST_TOOLS_MOCK_LOAD_BALANCER_ENCODER_H_

#include "quiche/quic/load_balancer/load_balancer_encoder.h"
#include "quiche/quic/platform/api/quic_test.h"

namespace quic {
namespace test {

class MockLoadBalancerEncoder : public LoadBalancerEncoder {
 public:
  MockLoadBalancerEncoder()
      : LoadBalancerEncoder(*QuicRandom::GetInstance(), nullptr, false,
                            kLoadBalancerUnroutableLen) {}
  MOCK_METHOD(bool, IsEncoding, (), (const, override));
  MOCK_METHOD(bool, IsEncrypted, (), (const, override));
  MOCK_METHOD(bool, len_self_encoded, (), (const, override));
  MOCK_METHOD(std::optional<QuicConnectionId>, GenerateNextConnectionId,
              (const QuicConnectionId& original), (override));
  MOCK_METHOD(std::optional<QuicConnectionId>, MaybeReplaceConnectionId,
              (const QuicConnectionId& original,
               const ParsedQuicVersion& version),
              (override));
  MOCK_METHOD(uint8_t, ConnectionIdLength, (uint8_t first_byte),
              (const, override));
  MOCK_METHOD(void, DeleteConfig, (), (override));
};

}  // namespace test
}  // namespace quic

#endif  // QUICHE_QUIC_TEST_TOOLS_MOCK_LOAD_BALANCER_ENCODER_H_
