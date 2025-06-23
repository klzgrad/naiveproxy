// Copyright (c) 2021 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef QUICHE_QUIC_TEST_TOOLS_QUIC_TEST_BACKEND_H_
#define QUICHE_QUIC_TEST_TOOLS_QUIC_TEST_BACKEND_H_

#include "quiche/quic/tools/quic_memory_cache_backend.h"
#include "quiche/common/http/http_header_block.h"
#include "quiche/common/platform/api/quiche_logging.h"

namespace quic {
namespace test {

// QuicTestBackend is a QuicSimpleServer backend usable in tests.  It has extra
// WebTransport endpoints on top of what QuicMemoryCacheBackend already
// provides.
class QuicTestBackend : public QuicMemoryCacheBackend {
 public:
  WebTransportResponse ProcessWebTransportRequest(
      const quiche::HttpHeaderBlock& request_headers,
      WebTransportSession* session) override;
  bool SupportsWebTransport() override { return enable_webtransport_; }

  void set_enable_webtransport(bool enable_webtransport) {
    QUICHE_DCHECK(!enable_webtransport || enable_extended_connect_);
    enable_webtransport_ = enable_webtransport;
  }

  bool SupportsExtendedConnect() override { return enable_extended_connect_; }

  void set_enable_extended_connect(bool enable_extended_connect) {
    enable_extended_connect_ = enable_extended_connect;
  }

 private:
  bool enable_webtransport_ = false;
  bool enable_extended_connect_ = true;
};

}  // namespace test
}  // namespace quic

#endif  // QUICHE_QUIC_TEST_TOOLS_QUIC_TEST_BACKEND_H_
