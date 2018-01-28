// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/spdy/chromium/spdy_log_util.h"

#include "base/strings/string_number_conversions.h"
#include "base/values.h"
#include "net/http/http_log_util.h"

namespace net {

SpdyString ElideGoAwayDebugDataForNetLog(NetLogCaptureMode capture_mode,
                                         base::StringPiece debug_data) {
  // Note: this logic should be kept in sync with stripGoAwayDebugData in
  // chrome/browser/resources/net_internals/log_view_painter.js.
  if (capture_mode.include_cookies_and_credentials()) {
    return debug_data.as_string();
  }

  return SpdyString("[") + base::NumberToString(debug_data.size()) +
         SpdyString(" bytes were stripped]");
}

std::unique_ptr<base::ListValue> ElideSpdyHeaderBlockForNetLog(
    const SpdyHeaderBlock& headers,
    NetLogCaptureMode capture_mode) {
  auto headers_list = std::make_unique<base::ListValue>();
  for (SpdyHeaderBlock::const_iterator it = headers.begin();
       it != headers.end(); ++it) {
    headers_list->AppendString(
        it->first.as_string() + ": " +
        ElideHeaderValueForNetLog(capture_mode, it->first.as_string(),
                                  it->second.as_string()));
  }
  return headers_list;
}

}  // namespace net
