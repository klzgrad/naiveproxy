// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef QUICHE_QUIC_TEST_TOOLS_MOCK_RANDOM_H_
#define QUICHE_QUIC_TEST_TOOLS_MOCK_RANDOM_H_

#include "quiche/quic/core/crypto/quic_random.h"
#include "quiche/quic/platform/api/quic_test.h"

namespace quic {
namespace test {

class MockRandom : public QuicRandom {
 public:
  // Initializes base_ to 0xDEADBEEF.
  MockRandom();
  explicit MockRandom(uint32_t base);
  MockRandom(const MockRandom&) = delete;
  MockRandom& operator=(const MockRandom&) = delete;

  MOCK_METHOD(void, RandBytes, (void* data, size_t len), (override));
  MOCK_METHOD(uint64_t, RandUint64, (), (override));
  MOCK_METHOD(void, InsecureRandBytes, (void* data, size_t len), (override));
  MOCK_METHOD(uint64_t, InsecureRandUint64, (), (override));

  // Default QuicRandom implementations. They are used if the caller does not
  // setup the MockRandom via EXPECT_CALLs.

  // Fills the |data| buffer with a repeating byte, initially 'r'.
  void DefaultRandBytes(void* data, size_t len);
  // Returns base + the current increment.
  uint64_t DefaultRandUint64();

  // InsecureRandBytes behaves equivalently to RandBytes.
  void DefaultInsecureRandBytes(void* data, size_t len);
  // InsecureRandUint64 behaves equivalently to RandUint64.
  uint64_t DefaultInsecureRandUint64();

  // ChangeValue increments |increment_|. This causes the value returned by
  // |RandUint64| and the byte that |RandBytes| fills with, to change.
  // Used by the Default implementations.
  void ChangeValue();

  // Sets the base to |base| and resets increment to zero.
  // Used by the Default implementations.
  void ResetBase(uint32_t base);

 private:
  uint32_t base_;
  uint8_t increment_;
};

}  // namespace test
}  // namespace quic

#endif  // QUICHE_QUIC_TEST_TOOLS_MOCK_RANDOM_H_
