// Copyright (c) 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/spdy/chromium/header_coalescer.h"

#include <memory>
#include <utility>

#include "base/bind.h"
#include "base/values.h"
#include "net/base/escape.h"
#include "net/http/http_log_util.h"
#include "net/http/http_util.h"
#include "net/spdy/platform/api/spdy_estimate_memory_usage.h"
#include "net/spdy/platform/api/spdy_string.h"

namespace net {
namespace {
std::unique_ptr<base::Value> ElideNetLogHeaderCallback(
    SpdyStringPiece header_name,
    SpdyStringPiece header_value,
    SpdyStringPiece error_message,
    NetLogCaptureMode capture_mode) {
  auto dict = std::make_unique<base::DictionaryValue>();
  dict->SetString("header_name", EscapeExternalHandlerValue(header_name));
  dict->SetString(
      "header_value",
      EscapeExternalHandlerValue(ElideHeaderValueForNetLog(
          capture_mode, header_name.as_string(), header_value.as_string())));
  dict->SetString("error", error_message);
  return std::move(dict);
}

}  // namespace

HeaderCoalescer::HeaderCoalescer(uint32_t max_header_list_size,
                                 const NetLogWithSource& net_log)
    : max_header_list_size_(max_header_list_size), net_log_(net_log) {}

void HeaderCoalescer::OnHeader(SpdyStringPiece key, SpdyStringPiece value) {
  if (error_seen_)
    return;
  if (!AddHeader(key, value))
    error_seen_ = true;
}

SpdyHeaderBlock HeaderCoalescer::release_headers() {
  DCHECK(headers_valid_);
  headers_valid_ = false;
  return std::move(headers_);
}

size_t HeaderCoalescer::EstimateMemoryUsage() const {
  return SpdyEstimateMemoryUsage(headers_);
}

bool HeaderCoalescer::AddHeader(SpdyStringPiece key, SpdyStringPiece value) {
  if (key.empty()) {
    net_log_.AddEvent(NetLogEventType::HTTP2_SESSION_RECV_INVALID_HEADER,
                      base::Bind(&ElideNetLogHeaderCallback, key, value,
                                 "Header name must not be empty."));
    return false;
  }

  SpdyStringPiece key_name = key;
  if (key[0] == ':') {
    if (regular_header_seen_) {
      net_log_.AddEvent(
          NetLogEventType::HTTP2_SESSION_RECV_INVALID_HEADER,
          base::Bind(&ElideNetLogHeaderCallback, key, value,
                     "Pseudo header must not follow regular headers."));
      return false;
    }
    key_name.remove_prefix(1);
  } else if (!regular_header_seen_) {
    regular_header_seen_ = true;
  }

  if (!HttpUtil::IsValidHeaderName(key_name)) {
    net_log_.AddEvent(NetLogEventType::HTTP2_SESSION_RECV_INVALID_HEADER,
                      base::Bind(&ElideNetLogHeaderCallback, key, value,
                                 "Invalid character in header name."));
    return false;
  }

  // 32 byte overhead according to RFC 7540 Section 6.5.2.
  header_list_size_ += key.size() + value.size() + 32;
  if (header_list_size_ > max_header_list_size_) {
    net_log_.AddEvent(NetLogEventType::HTTP2_SESSION_RECV_INVALID_HEADER,
                      base::Bind(&ElideNetLogHeaderCallback, key, value,
                                 "Header list too large."));
    return false;
  }

  // End of line delimiter is forbidden according to RFC 7230 Section 3.2.
  // Line folding, RFC 7230 Section 3.2.4., is a special case of this.
  if (value.find("\r\n") != SpdyStringPiece::npos) {
    net_log_.AddEvent(NetLogEventType::HTTP2_SESSION_RECV_INVALID_HEADER,
                      base::Bind(&ElideNetLogHeaderCallback, key, value,
                                 "Header value must not contain CR+LF."));
    return false;
  }

  auto iter = headers_.find(key);
  if (iter == headers_.end()) {
    headers_[key] = value;
  } else {
    // This header had multiple values, so it must be reconstructed.
    SpdyStringPiece v = iter->second;
    SpdyString s(v.data(), v.length());
    if (key == "cookie") {
      // Obeys section 8.1.2.5 in RFC 7540 for cookie reconstruction.
      s.append("; ");
    } else {
      SpdyStringPiece("\0", 1).AppendToString(&s);
    }
    value.AppendToString(&s);
    headers_[key] = s;
  }
  return true;
}


}  // namespace net
