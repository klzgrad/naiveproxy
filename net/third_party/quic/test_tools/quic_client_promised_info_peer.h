// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef NET_THIRD_PARTY_QUIC_TEST_TOOLS_QUIC_CLIENT_PROMISED_INFO_PEER_H_
#define NET_THIRD_PARTY_QUIC_TEST_TOOLS_QUIC_CLIENT_PROMISED_INFO_PEER_H_

#include "base/macros.h"
#include "net/third_party/quic/core/http/quic_client_promised_info.h"

namespace quic {
namespace test {

class QuicClientPromisedInfoPeer {
 public:
  QuicClientPromisedInfoPeer() = delete;

  static QuicAlarm* GetAlarm(QuicClientPromisedInfo* promised_stream);
};
}  // namespace test
}  // namespace quic

#endif  // NET_THIRD_PARTY_QUIC_TEST_TOOLS_QUIC_CLIENT_PROMISED_INFO_PEER_H_
