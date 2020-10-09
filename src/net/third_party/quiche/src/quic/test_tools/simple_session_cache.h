// Copyright (c) 2019 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef QUICHE_QUIC_TEST_TOOLS_SIMPLE_SESSION_CACHE_H_
#define QUICHE_QUIC_TEST_TOOLS_SIMPLE_SESSION_CACHE_H_

#include <memory>
#include "net/third_party/quiche/src/quic/core/crypto/quic_crypto_client_config.h"
#include "net/third_party/quiche/src/quic/core/crypto/transport_parameters.h"

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
              bssl::UniquePtr<SSL_SESSION> session,
              const TransportParameters& params,
              const ApplicationState* application_state) override;
  std::unique_ptr<QuicResumptionState> Lookup(const QuicServerId& server_id,
                                              const SSL_CTX* ctx) override;
  void ClearEarlyData(const QuicServerId& server_id) override;

 private:
  struct Entry {
    bssl::UniquePtr<SSL_SESSION> session;
    std::unique_ptr<TransportParameters> params;
    std::unique_ptr<ApplicationState> application_state;
  };
  std::map<QuicServerId, Entry> cache_entries_;
};

}  // namespace test
}  // namespace quic

#endif  // QUICHE_QUIC_TEST_TOOLS_SIMPLE_SESSION_CACHE_H_
