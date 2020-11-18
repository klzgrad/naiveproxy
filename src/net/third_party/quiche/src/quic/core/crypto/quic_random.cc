// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/third_party/quiche/src/quic/core/crypto/quic_random.h"

#include "third_party/boringssl/src/include/openssl/rand.h"
#include "net/third_party/quiche/src/quic/platform/api/quic_bug_tracker.h"

namespace quic {

namespace {

class DefaultRandom : public QuicRandom {
 public:
  DefaultRandom() {}
  DefaultRandom(const DefaultRandom&) = delete;
  DefaultRandom& operator=(const DefaultRandom&) = delete;
  ~DefaultRandom() override {}

  // QuicRandom implementation
  void RandBytes(void* data, size_t len) override;
  uint64_t RandUint64() override;
};

void DefaultRandom::RandBytes(void* data, size_t len) {
  RAND_bytes(reinterpret_cast<uint8_t*>(data), len);
}

uint64_t DefaultRandom::RandUint64() {
  uint64_t value;
  RandBytes(&value, sizeof(value));
  return value;
}

}  // namespace

// static
QuicRandom* QuicRandom::GetInstance() {
  static DefaultRandom* random = new DefaultRandom();
  return random;
}

}  // namespace quic
