// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/spdy/spdy_log_util.h"

#include <utility>

#include "base/strings/string_number_conversions.h"
#include "base/values.h"
#include "net/http/http_log_util.h"

namespace net {

std::string ElideGoAwayDebugDataForNetLog(NetLogCaptureMode capture_mode,
                                          base::StringPiece debug_data) {
  if (capture_mode.include_cookies_and_credentials()) {
    return debug_data.as_string();
  }

  return std::string("[") + base::NumberToString(debug_data.size()) +
         std::string(" bytes were stripped]");
}

std::unique_ptr<base::ListValue> ElideSpdyHeaderBlockForNetLog(
    const spdy::SpdyHeaderBlock& headers,
    NetLogCaptureMode capture_mode) {
  auto headers_list = std::make_unique<base::ListValue>();
  for (spdy::SpdyHeaderBlock::const_iterator it = headers.begin();
       it != headers.end(); ++it) {
    headers_list->AppendString(
        it->first.as_string() + ": " +
        ElideHeaderValueForNetLog(capture_mode, it->first.as_string(),
                                  it->second.as_string()));
  }
  return headers_list;
}

std::unique_ptr<base::Value> SpdyHeaderBlockNetLogCallback(
    const spdy::SpdyHeaderBlock* headers,
    NetLogCaptureMode capture_mode) {
  auto dict = std::make_unique<base::DictionaryValue>();
  auto headers_dict = std::make_unique<base::DictionaryValue>();
  for (spdy::SpdyHeaderBlock::const_iterator it = headers->begin();
       it != headers->end(); ++it) {
    headers_dict->SetKey(
        it->first.as_string(),
        base::Value(ElideHeaderValueForNetLog(
            capture_mode, it->first.as_string(), it->second.as_string())));
  }
  dict->Set("headers", std::move(headers_dict));
  return std::move(dict);
}

}  // namespace net
