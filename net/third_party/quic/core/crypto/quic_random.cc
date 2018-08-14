// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/third_party/quic/core/crypto/quic_random.h"

#include "base/macros.h"
#include "crypto/random.h"
#include "net/third_party/quic/platform/api/quic_bug_tracker.h"
#include "net/third_party/quic/platform/api/quic_singleton.h"

namespace quic {

namespace {

class DefaultRandom : public QuicRandom {
 public:
  static DefaultRandom* GetInstance();

  // QuicRandom implementation
  void RandBytes(void* data, size_t len) override;
  uint64_t RandUint64() override;

 private:
  DefaultRandom() {}
  DefaultRandom(const DefaultRandom&) = delete;
  DefaultRandom& operator=(const DefaultRandom&) = delete;
  ~DefaultRandom() override {}

  friend QuicSingletonFriend<DefaultRandom>;
};

DefaultRandom* DefaultRandom::GetInstance() {
  return QuicSingleton<DefaultRandom>::get();
}

void DefaultRandom::RandBytes(void* data, size_t len) {
  crypto::RandBytes(data, len);
}

uint64_t DefaultRandom::RandUint64() {
  uint64_t value;
  RandBytes(&value, sizeof(value));
  return value;
}

}  // namespace

// static
QuicRandom* QuicRandom::GetInstance() {
  return DefaultRandom::GetInstance();
}

}  // namespace quic
