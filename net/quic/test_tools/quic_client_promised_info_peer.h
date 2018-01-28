// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef NET_QUIC_TEST_TOOLS_QUIC_CLIENT_PROMISED_INFO_PEER_H_
#define NET_QUIC_TEST_TOOLS_QUIC_CLIENT_PROMISED_INFO_PEER_H_

#include "base/macros.h"
#include "net/quic/core/quic_client_promised_info.h"

namespace net {
namespace test {

class QuicClientPromisedInfoPeer {
 public:
  static QuicAlarm* GetAlarm(QuicClientPromisedInfo* promised_stream);

 private:
  DISALLOW_COPY_AND_ASSIGN(QuicClientPromisedInfoPeer);
};
}  // namespace test
}  // namespace net

#endif  // NET_QUIC_TEST_TOOLS_QUIC_CLIENT_PROMISED_INFO_PEER_H_
