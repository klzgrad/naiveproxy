// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef QUICHE_HTTP2_CORE_SPDY_HEADERS_HANDLER_INTERFACE_H_
#define QUICHE_HTTP2_CORE_SPDY_HEADERS_HANDLER_INTERFACE_H_

#include <stddef.h>

#include "absl/strings/string_view.h"
#include "quiche/common/platform/api/quiche_export.h"

namespace spdy {

// This interface defines how an object that accepts header data should behave.
// It is used by both SpdyHeadersBlockParser and HpackDecoder.
class QUICHE_EXPORT SpdyHeadersHandlerInterface {
 public:
  virtual ~SpdyHeadersHandlerInterface() {}

  // A callback method which notifies when the parser starts handling a new
  // header block. Will only be called once per block, even if it extends into
  // CONTINUATION frames.
  virtual void OnHeaderBlockStart() = 0;

  // A callback method which notifies on a header key value pair. Multiple
  // values for a given key will be emitted as multiple calls to OnHeader.
  virtual void OnHeader(absl::string_view key, absl::string_view value) = 0;

  // A callback method which notifies when the parser finishes handling a
  // header block (i.e. the containing frame has the END_HEADERS flag set).
  // Also indicates the total number of bytes in this block.
  virtual void OnHeaderBlockEnd(size_t uncompressed_header_bytes,
                                size_t compressed_header_bytes) = 0;
};

}  // namespace spdy

#endif  // QUICHE_HTTP2_CORE_SPDY_HEADERS_HANDLER_INTERFACE_H_
