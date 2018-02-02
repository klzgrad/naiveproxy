// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef NET_QUIC_TEST_TOOLS_MOCK_RANDOM_H_
#define NET_QUIC_TEST_TOOLS_MOCK_RANDOM_H_

#include "base/compiler_specific.h"
#include "base/macros.h"
#include "net/quic/core/crypto/quic_random.h"

namespace net {
namespace test {

class MockRandom : public QuicRandom {
 public:
  // Initializes base_ to 0xDEADBEEF.
  MockRandom();
  explicit MockRandom(uint32_t base);

  // QuicRandom:
  // Fills the |data| buffer with a repeating byte, initially 'r'.
  void RandBytes(void* data, size_t len) override;
  // Returns base + the current increment.
  uint64_t RandUint64() override;
  // Does nothing.
  void Reseed(const void* additional_entropy, size_t entropy_len) override;

  // ChangeValue increments |increment_|. This causes the value returned by
  // |RandUint64| and the byte that |RandBytes| fills with, to change.
  void ChangeValue();

 private:
  uint32_t base_;
  uint8_t increment_;

  DISALLOW_COPY_AND_ASSIGN(MockRandom);
};

}  // namespace test
}  // namespace net

#endif  // NET_QUIC_TEST_TOOLS_MOCK_RANDOM_H_
