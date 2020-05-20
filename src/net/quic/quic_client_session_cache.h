// Copyright (c) 2020 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef NET_QUIC_QUIC_CLIENT_SESSION_CACHE_H_
#define NET_QUIC_QUIC_CLIENT_SESSION_CACHE_H_

#include <stddef.h>
#include <time.h>

#include <memory>
#include <string>

#include "base/bind.h"
#include "base/containers/mru_cache.h"
#include "base/memory/memory_pressure_monitor.h"
#include "base/time/time.h"
#include "net/third_party/quiche/src/quic/core/crypto/quic_crypto_client_config.h"
#include "third_party/boringssl/src/include/openssl/base.h"

namespace base {
class Clock;
}

namespace net {

class NET_EXPORT_PRIVATE QuicClientSessionCache : public quic::SessionCache {
 public:
  QuicClientSessionCache();
  explicit QuicClientSessionCache(size_t max_entries);
  ~QuicClientSessionCache() override;

  void Insert(const quic::QuicServerId& server_id,
              std::unique_ptr<quic::QuicResumptionState> state) override;

  std::unique_ptr<quic::QuicResumptionState> Lookup(
      const quic::QuicServerId& server_id,
      const SSL_CTX* ctx) override;

  void SetClockForTesting(base::Clock* clock) { clock_ = clock; }

  size_t size() const { return cache_.size(); }

  void Flush() { cache_.Clear(); }

  void OnMemoryPressure(
      base::MemoryPressureListener::MemoryPressureLevel memory_pressure_level);

 private:
  void FlushExpiredStates();

  base::Clock* clock_;
  base::MRUCache<quic::QuicServerId, std::unique_ptr<quic::QuicResumptionState>>
      cache_;
  std::unique_ptr<base::MemoryPressureListener> memory_pressure_listener_;
};

}  // namespace net

#endif  // NET_QUIC_QUIC_CLIENT_SESSION_CACHE_H_
