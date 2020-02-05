// Copyright (c) 2019 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef QUICHE_QUIC_TEST_TOOLS_SIMPLE_SESSION_CACHE_H_
#define QUICHE_QUIC_TEST_TOOLS_SIMPLE_SESSION_CACHE_H_

#include "net/third_party/quiche/src/quic/core/crypto/quic_crypto_client_config.h"

namespace quic {
namespace test {

// SimpleSessionCache provides a simple implementation of SessionCache that
// stores only one QuicResumptionState per QuicServerId. No limit is placed on
// the total number of entries in the cache. When Lookup is called, if a cache
// entry exists for the provided QuicServerId, the entry will be removed from
// the cached when it is returned.
class SimpleSessionCache : public SessionCache {
 public:
  SimpleSessionCache() = default;
  ~SimpleSessionCache() override = default;

  void Insert(const QuicServerId& server_id,
              std::unique_ptr<QuicResumptionState> state) override;
  std::unique_ptr<QuicResumptionState> Lookup(const QuicServerId& server_id,
                                              const SSL_CTX* ctx) override;

 private:
  std::map<QuicServerId, std::unique_ptr<QuicResumptionState>> cache_entries_;
};

}  // namespace test
}  // namespace quic

#endif  // QUICHE_QUIC_TEST_TOOLS_SIMPLE_SESSION_CACHE_H_
