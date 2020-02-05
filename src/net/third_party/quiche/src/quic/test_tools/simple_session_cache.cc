// Copyright (c) 2019 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/third_party/quiche/src/quic/test_tools/simple_session_cache.h"

namespace quic {
namespace test {

void SimpleSessionCache::Insert(const QuicServerId& server_id,
                                std::unique_ptr<QuicResumptionState> state) {
  cache_entries_.insert(std::make_pair(server_id, std::move(state)));
}

std::unique_ptr<QuicResumptionState> SimpleSessionCache::Lookup(
    const QuicServerId& server_id,
    const SSL_CTX* /*ctx*/) {
  auto it = cache_entries_.find(server_id);
  if (it == cache_entries_.end()) {
    return nullptr;
  }
  std::unique_ptr<QuicResumptionState> state = std::move(it->second);
  cache_entries_.erase(it);
  return state;
}

}  // namespace test
}  // namespace quic
