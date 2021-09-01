// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "quic/core/crypto/quic_random.h"
#include <cstdint>
#include <cstring>

#include "third_party/boringssl/src/include/openssl/rand.h"
#include "quic/platform/api/quic_bug_tracker.h"
#include "quic/platform/api/quic_logging.h"
#include "common/platform/api/quiche_logging.h"

namespace quic {

namespace {

// Insecure randomness in DefaultRandom uses an implementation of
// xoshiro256++ 1.0 based on code in the public domain from
// <http://prng.di.unimi.it/xoshiro256plusplus.c>.

inline uint64_t Xoshiro256PlusPlusRotLeft(uint64_t x, int k) {
  return (x << k) | (x >> (64 - k));
}

uint64_t Xoshiro256PlusPlus() {
  static thread_local uint64_t rng_state[4];
  static thread_local bool rng_state_initialized = false;
  if (QUIC_PREDICT_FALSE(!rng_state_initialized)) {
    RAND_bytes(reinterpret_cast<uint8_t*>(&rng_state), sizeof(rng_state));
    rng_state_initialized = true;
  }
  const uint64_t result =
      Xoshiro256PlusPlusRotLeft(rng_state[0] + rng_state[3], 23) + rng_state[0];
  const uint64_t t = rng_state[1] << 17;
  rng_state[2] ^= rng_state[0];
  rng_state[3] ^= rng_state[1];
  rng_state[1] ^= rng_state[2];
  rng_state[0] ^= rng_state[3];
  rng_state[2] ^= t;
  rng_state[3] = Xoshiro256PlusPlusRotLeft(rng_state[3], 45);
  return result;
}

class DefaultRandom : public QuicRandom {
 public:
  DefaultRandom() {}
  DefaultRandom(const DefaultRandom&) = delete;
  DefaultRandom& operator=(const DefaultRandom&) = delete;
  ~DefaultRandom() override {}

  // QuicRandom implementation
  void RandBytes(void* data, size_t len) override;
  uint64_t RandUint64() override;
  void InsecureRandBytes(void* data, size_t len) override;
  uint64_t InsecureRandUint64() override;
};

void DefaultRandom::RandBytes(void* data, size_t len) {
  RAND_bytes(reinterpret_cast<uint8_t*>(data), len);
}

uint64_t DefaultRandom::RandUint64() {
  uint64_t value;
  RandBytes(&value, sizeof(value));
  return value;
}

void DefaultRandom::InsecureRandBytes(void* data, size_t len) {
  while (len >= sizeof(uint64_t)) {
    uint64_t random_bytes64 = Xoshiro256PlusPlus();
    memcpy(data, &random_bytes64, sizeof(uint64_t));
    data = reinterpret_cast<char*>(data) + sizeof(uint64_t);
    len -= sizeof(uint64_t);
  }
  if (len > 0) {
    QUICHE_DCHECK_LT(len, sizeof(uint64_t));
    uint64_t random_bytes64 = Xoshiro256PlusPlus();
    memcpy(data, &random_bytes64, len);
  }
}

uint64_t DefaultRandom::InsecureRandUint64() {
  return Xoshiro256PlusPlus();
}

}  // namespace

// static
QuicRandom* QuicRandom::GetInstance() {
  static DefaultRandom* random = new DefaultRandom();
  return random;
}

}  // namespace quic
