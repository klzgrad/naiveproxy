// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "quiche/quic/test_tools/mock_random.h"

#include <string.h>

namespace quic {
namespace test {

using testing::_;
using testing::Invoke;

MockRandom::MockRandom() : MockRandom(0xDEADBEEF) {}

MockRandom::MockRandom(uint32_t base) : base_(base), increment_(0) {
  ON_CALL(*this, RandBytes(_, _))
      .WillByDefault(Invoke(this, &MockRandom::DefaultRandBytes));
  ON_CALL(*this, RandUint64())
      .WillByDefault(Invoke(this, &MockRandom::DefaultRandUint64));
  ON_CALL(*this, InsecureRandBytes(_, _))
      .WillByDefault(Invoke(this, &MockRandom::DefaultInsecureRandBytes));
  ON_CALL(*this, InsecureRandUint64())
      .WillByDefault(Invoke(this, &MockRandom::DefaultInsecureRandUint64));
}

void MockRandom::DefaultRandBytes(void* data, size_t len) {
  memset(data, increment_ + static_cast<uint8_t>('r'), len);
}

uint64_t MockRandom::DefaultRandUint64() { return base_ + increment_; }

void MockRandom::DefaultInsecureRandBytes(void* data, size_t len) {
  DefaultRandBytes(data, len);
}

uint64_t MockRandom::DefaultInsecureRandUint64() { return DefaultRandUint64(); }

void MockRandom::ChangeValue() { increment_++; }

void MockRandom::ResetBase(uint32_t base) {
  base_ = base;
  increment_ = 0;
}

}  // namespace test
}  // namespace quic
