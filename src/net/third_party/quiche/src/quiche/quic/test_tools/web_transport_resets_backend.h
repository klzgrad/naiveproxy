// Copyright (c) 2021 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef QUICHE_QUIC_TEST_TOOLS_WEB_TRANSPORT_RESETS_BACKEND_H_
#define QUICHE_QUIC_TEST_TOOLS_WEB_TRANSPORT_RESETS_BACKEND_H_

#include "quiche/quic/test_tools/quic_test_backend.h"
#include "quiche/spdy/core/http2_header_block.h"

namespace quic {
namespace test {

// A backend for testing RESET_STREAM/STOP_SENDING behavior.  Provides
// bidirectional echo streams; whenever one of those receives RESET_STREAM or
// STOP_SENDING, a log message is sent as a unidirectional stream.
QuicSimpleServerBackend::WebTransportResponse WebTransportResetsBackend(
    const spdy::Http2HeaderBlock& request_headers,
    WebTransportSession* session);

}  // namespace test
}  // namespace quic

#endif  // QUICHE_QUIC_TEST_TOOLS_WEB_TRANSPORT_RESETS_BACKEND_H_
