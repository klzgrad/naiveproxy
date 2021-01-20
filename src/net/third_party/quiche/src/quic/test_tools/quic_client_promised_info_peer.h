// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef QUICHE_QUIC_TEST_TOOLS_QUIC_CLIENT_PROMISED_INFO_PEER_H_
#define QUICHE_QUIC_TEST_TOOLS_QUIC_CLIENT_PROMISED_INFO_PEER_H_

#include "net/third_party/quiche/src/quic/core/http/quic_client_promised_info.h"

namespace quic {
namespace test {

class QuicClientPromisedInfoPeer {
 public:
  QuicClientPromisedInfoPeer() = delete;

  static QuicAlarm* GetAlarm(QuicClientPromisedInfo* promised_stream);
};
}  // namespace test
}  // namespace quic

#endif  // QUICHE_QUIC_TEST_TOOLS_QUIC_CLIENT_PROMISED_INFO_PEER_H_
