// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/quic/platform/api/quic_test_random.h"

#include <memory>

#include "base/rand_util.h"

namespace net {
namespace test {

bool QuicTestRandom::OneIn(int n) {
  return base::RandGenerator(n) == 0;
}

int32_t QuicTestRandom::Uniform(int32_t n) {
  return base::RandGenerator(n);
}

uint8_t QuicTestRandom::Rand8() {
  return base::RandGenerator(
      static_cast<uint64_t>(std::numeric_limits<uint8_t>::max()) + 1);
}

uint16_t QuicTestRandom::Rand16() {
  return base::RandGenerator(
      static_cast<uint64_t>(std::numeric_limits<uint16_t>::max()) + 1);
}

uint32_t QuicTestRandom::Rand32() {
  return base::RandGenerator(
      static_cast<uint64_t>(std::numeric_limits<uint32_t>::max()) + 1);
}

uint64_t QuicTestRandom::Rand64() {
  return base::RandUint64();
}

int32_t QuicTestRandom::Next() {
  return Rand32();
}

int32_t QuicTestRandom::Skewed(int max_log) {
  const uint32_t base = Rand32() % (max_log + 1);
  const uint32_t mask = ((base < 32) ? (1u << base) : 0u) - 1u;
  return Rand32() & mask;
}

QuicString QuicTestRandom::RandString(int length) {
  std::unique_ptr<char[]> buffer(new char[length]);
  base::RandBytes(buffer.get(), length);
  return QuicString(buffer.get(), length);
}

}  // namespace test
}  // namespace net
