// Copyright (c) 2021 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef QUICHE_QUIC_TEST_TOOLS_QUIC_TEST_BACKEND_H_
#define QUICHE_QUIC_TEST_TOOLS_QUIC_TEST_BACKEND_H_

#include "quic/tools/quic_memory_cache_backend.h"

namespace quic {
namespace test {

// QuicTestBackend is a QuicSimpleServer backend usable in tests.  It has extra
// WebTransport endpoints on top of what QuicMemoryCacheBackend already
// provides.
class QuicTestBackend : public QuicMemoryCacheBackend {
 public:
  WebTransportResponse ProcessWebTransportRequest(
      const spdy::Http2HeaderBlock& request_headers,
      WebTransportSession* session) override;
  bool SupportsWebTransport() override { return enable_webtransport_; }

  void set_enable_webtransport(bool enable_webtransport) {
    enable_webtransport_ = enable_webtransport;
  }

 private:
  bool enable_webtransport_ = false;
};

}  // namespace test
}  // namespace quic

#endif  // QUICHE_QUIC_TEST_TOOLS_QUIC_TEST_BACKEND_H_
