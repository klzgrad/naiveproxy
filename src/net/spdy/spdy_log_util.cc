// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/spdy/spdy_log_util.h"

#include <utility>

#include "base/strings/strcat.h"
#include "base/strings/string_number_conversions.h"
#include "base/values.h"
#include "net/http/http_log_util.h"

namespace net {

base::Value ElideGoAwayDebugDataForNetLog(NetLogCaptureMode capture_mode,
                                          base::StringPiece debug_data) {
  if (capture_mode.include_cookies_and_credentials())
    return NetLogStringValue(debug_data);

  return NetLogStringValue(base::StrCat(
      {"[", base::NumberToString(debug_data.size()), " bytes were stripped]"}));
}

std::unique_ptr<base::ListValue> ElideSpdyHeaderBlockForNetLog(
    const spdy::SpdyHeaderBlock& headers,
    NetLogCaptureMode capture_mode) {
  auto headers_list = std::make_unique<base::ListValue>();
  for (const auto& header : headers) {
    base::StringPiece key = header.first;
    base::StringPiece value = header.second;
    headers_list->GetList().push_back(NetLogStringValue(
        base::StrCat({key, ": ",
                      ElideHeaderValueForNetLog(capture_mode, key.as_string(),
                                                value.as_string())})));
  }
  return headers_list;
}

std::unique_ptr<base::Value> SpdyHeaderBlockNetLogCallback(
    const spdy::SpdyHeaderBlock* headers,
    NetLogCaptureMode capture_mode) {
  auto dict = std::make_unique<base::DictionaryValue>();
  dict->Set("headers", ElideSpdyHeaderBlockForNetLog(*headers, capture_mode));
  return std::move(dict);
}

}  // namespace net
