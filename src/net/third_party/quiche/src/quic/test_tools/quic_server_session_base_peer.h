// Copyright (c) 2018 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef QUICHE_QUIC_TEST_TOOLS_QUIC_SERVER_SESSION_BASE_PEER_H_
#define QUICHE_QUIC_TEST_TOOLS_QUIC_SERVER_SESSION_BASE_PEER_H_

#include "net/third_party/quiche/src/quic/core/http/quic_server_session_base.h"

#include "net/third_party/quiche/src/quic/core/quic_utils.h"

namespace quic {
namespace test {

class QuicServerSessionBasePeer {
 public:
  static QuicStream* GetOrCreateStream(QuicServerSessionBase* s,
                                       QuicStreamId id) {
    return s->GetOrCreateStream(id);
  }
  static void SetCryptoStream(QuicServerSessionBase* s,
                              QuicCryptoServerStreamBase* crypto_stream) {
    s->crypto_stream_.reset(crypto_stream);
  }
  static bool IsBandwidthResumptionEnabled(QuicServerSessionBase* s) {
    return s->bandwidth_resumption_enabled_;
  }
};

}  // namespace test

}  // namespace quic

#endif  // QUICHE_QUIC_TEST_TOOLS_QUIC_SERVER_SESSION_BASE_PEER_H_
