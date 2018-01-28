// Copyright (c) 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef NET_SPDY_CHROMIUM_HEADER_COALESCER_H_
#define NET_SPDY_CHROMIUM_HEADER_COALESCER_H_

#include "net/base/net_export.h"
#include "net/log/net_log_with_source.h"
#include "net/spdy/core/spdy_header_block.h"
#include "net/spdy/core/spdy_headers_handler_interface.h"
#include "net/spdy/platform/api/spdy_string_piece.h"

namespace net {

class NET_EXPORT_PRIVATE HeaderCoalescer : public SpdyHeadersHandlerInterface {
 public:
  HeaderCoalescer(uint32_t max_header_list_size,
                  const NetLogWithSource& net_log);

  void OnHeaderBlockStart() override {}

  void OnHeader(SpdyStringPiece key, SpdyStringPiece value) override;

  void OnHeaderBlockEnd(size_t uncompressed_header_bytes,
                        size_t compressed_header_bytes) override {}

  SpdyHeaderBlock release_headers();
  bool error_seen() const { return error_seen_; }

  // Returns the estimate of dynamically allocated memory in bytes.
  size_t EstimateMemoryUsage() const;

 private:
  // Helper to add a header. Return true on success.
  bool AddHeader(SpdyStringPiece key, SpdyStringPiece value);

  SpdyHeaderBlock headers_;
  bool headers_valid_ = true;
  size_t header_list_size_ = 0;
  bool error_seen_ = false;
  bool regular_header_seen_ = false;
  const uint32_t max_header_list_size_;
  NetLogWithSource net_log_;
};

}  // namespace net

#endif  // NET_SPDY_CHROMIUM_HEADER_COALESCER_H_
