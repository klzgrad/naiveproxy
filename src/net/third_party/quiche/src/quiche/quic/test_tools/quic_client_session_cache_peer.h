// Copyright (c) 2021 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef QUICHE_QUIC_TEST_TOOLS_QUIC_CLIENT_SESSION_CACHE_PEER_H_
#define QUICHE_QUIC_TEST_TOOLS_QUIC_CLIENT_SESSION_CACHE_PEER_H_

#include "quiche/quic/core/crypto/quic_client_session_cache.h"

namespace quic {
namespace test {

class QuicClientSessionCachePeer {
 public:
  static std::string GetToken(QuicClientSessionCache* cache,
                              const QuicServerId& server_id) {
    auto iter = cache->cache_.Lookup(server_id);
    if (iter == cache->cache_.end()) {
      return {};
    }
    return iter->second->token;
  }

  static bool HasEntry(QuicClientSessionCache* cache,
                       const QuicServerId& server_id) {
    return cache->cache_.Lookup(server_id) != cache->cache_.end();
  }
};

}  // namespace test
}  // namespace quic

#endif  // QUICHE_QUIC_TEST_TOOLS_QUIC_CLIENT_SESSION_CACHE_PEER_H_
