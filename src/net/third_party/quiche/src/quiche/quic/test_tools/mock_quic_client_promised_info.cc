// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "quiche/quic/test_tools/mock_quic_client_promised_info.h"

namespace quic {
namespace test {

MockQuicClientPromisedInfo::MockQuicClientPromisedInfo(
    QuicSpdyClientSessionBase* session, QuicStreamId id, std::string url)
    : QuicClientPromisedInfo(session, id, url) {}

MockQuicClientPromisedInfo::~MockQuicClientPromisedInfo() {}

}  // namespace test
}  // namespace quic
