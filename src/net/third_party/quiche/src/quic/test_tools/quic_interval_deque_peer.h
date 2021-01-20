// Copyright 2019 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef QUICHE_QUIC_TEST_TOOLS_QUIC_INTERVAL_DEQUE_PEER_H_
#define QUICHE_QUIC_TEST_TOOLS_QUIC_INTERVAL_DEQUE_PEER_H_

#include "net/third_party/quiche/src/quic/core/quic_interval_deque.h"

namespace quic {

namespace test {

class QuicIntervalDequePeer {
 public:
  template <class T, class C>
  static int32_t GetCachedIndex(QuicIntervalDeque<T, C>* interval_deque) {
    if (!interval_deque->cached_index_.has_value()) {
      return -1;
    }
    return interval_deque->cached_index_.value();
  }

  template <class T, class C>
  static T* GetItem(QuicIntervalDeque<T, C>* interval_deque,
                    const std::size_t index) {
    return &interval_deque->container_[index];
  }
};

}  // namespace test

}  // namespace quic

#endif  // QUICHE_QUIC_TEST_TOOLS_QUIC_INTERVAL_DEQUE_PEER_H_
