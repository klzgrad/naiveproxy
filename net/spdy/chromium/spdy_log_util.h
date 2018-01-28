// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef NET_SPDY_CHROMIUM_SPDY_LOG_UTIL_H_
#define NET_SPDY_CHROMIUM_SPDY_LOG_UTIL_H_

#include <memory>

#include "base/strings/string_piece.h"
#include "net/base/net_export.h"
#include "net/log/net_log_capture_mode.h"
#include "net/spdy/core/spdy_header_block.h"
#include "net/spdy/platform/api/spdy_string.h"

namespace base {
class ListValue;
}  // namespace base

namespace net {

// Given an HTTP/2 GOAWAY frame |debug_data|, returns the elided version
// according to |capture_mode|.
NET_EXPORT_PRIVATE SpdyString
ElideGoAwayDebugDataForNetLog(NetLogCaptureMode capture_mode,
                              base::StringPiece debug_data);

// Given a SpdyHeaderBlock, return its base::ListValue representation.
NET_EXPORT_PRIVATE std::unique_ptr<base::ListValue>
ElideSpdyHeaderBlockForNetLog(const SpdyHeaderBlock& headers,
                              NetLogCaptureMode capture_mode);

}  // namespace net

#endif  // NET_SPDY_CHROMIUM_SPDY_LOG_UTIL_H_
