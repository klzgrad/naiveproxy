// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/third_party/quiche/src/quic/test_tools/quic_client_promised_info_peer.h"

namespace quic {
namespace test {

// static
QuicAlarm* QuicClientPromisedInfoPeer::GetAlarm(
    QuicClientPromisedInfo* promised_stream) {
  return promised_stream->cleanup_alarm_.get();
}

}  // namespace test
}  // namespace quic
