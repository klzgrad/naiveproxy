// Copyright (c) 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/spdy/chromium/header_coalescer.h"

#include <memory>
#include <utility>

#include "base/bind.h"
#include "base/strings/string_util.h"
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

bool ContainsUppercaseAscii(SpdyStringPiece str) {
  return std::any_of(str.begin(), str.end(), base::IsAsciiUpper<char>);
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

  if (ContainsUppercaseAscii(key_name)) {
    net_log_.AddEvent(NetLogEventType::HTTP2_SESSION_RECV_INVALID_HEADER,
                      base::Bind(&ElideNetLogHeaderCallback, key, value,
                                 "Upper case characters in header name."));
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

  // RFC 7540 Section 10.3: "Any request or response that contains a character
  // not permitted in a header field value MUST be treated as malformed (Section
  // 8.1.2.6). Valid characters are defined by the field-content ABNF rule in
  // Section 3.2 of [RFC7230]." RFC 7230 Section 3.2 says:
  // field-content  = field-vchar [ 1*( SP / HTAB ) field-vchar ]
  // field-vchar    = VCHAR / obs-text
  // RFC 5234 Appendix B.1 defines |VCHAR|:
  // VCHAR          =  %x21-7E
  // RFC 7230 Section 3.2.6 defines |obs-text|:
  // obs-text       = %x80-FF
  // Therefore allowed characters are '\t' (HTAB), x20 (SP), x21-7E, and x80-FF.
  for (const unsigned char c : value) {
    if (c < '\t' || ('\t' < c && c < 0x20) || c == 0x7f) {
      net_log_.AddEvent(NetLogEventType::HTTP2_SESSION_RECV_INVALID_HEADER,
                        base::Bind(&ElideNetLogHeaderCallback, key, value,
                                   "Invalid character in header value."));
      return false;
    }
  }

  headers_.AppendValueOrAddHeader(key, value);
  return true;
}


}  // namespace net
