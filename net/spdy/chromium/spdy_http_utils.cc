// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/spdy/chromium/spdy_http_utils.h"

#include <vector>

#include "base/strings/string_number_conversions.h"
#include "base/strings/string_split.h"
#include "base/strings/string_util.h"
#include "base/time/time.h"
#include "net/base/escape.h"
#include "net/base/load_flags.h"
#include "net/base/url_util.h"
#include "net/http/http_request_headers.h"
#include "net/http/http_request_info.h"
#include "net/http/http_response_headers.h"
#include "net/http/http_response_info.h"
#include "net/http/http_util.h"
#include "net/spdy/platform/api/spdy_string.h"
#include "net/spdy/platform/api/spdy_string_piece.h"

namespace net {

namespace {

void AddSpdyHeader(const SpdyString& name,
                   const SpdyString& value,
                   SpdyHeaderBlock* headers) {
  if (headers->find(name) == headers->end()) {
    (*headers)[name] = value;
  } else {
    SpdyString joint_value = (*headers)[name].as_string();
    joint_value.append(1, '\0');
    joint_value.append(value);
    (*headers)[name] = joint_value;
  }
}

}  // namespace

bool SpdyHeadersToHttpResponse(const SpdyHeaderBlock& headers,
                               HttpResponseInfo* response) {
  // The ":status" header is required.
  SpdyHeaderBlock::const_iterator it = headers.find(kHttp2StatusHeader);
  if (it == headers.end())
    return false;
  SpdyString status = it->second.as_string();
  SpdyString raw_headers("HTTP/1.1 ");
  raw_headers.append(status);
  raw_headers.push_back('\0');
  for (it = headers.begin(); it != headers.end(); ++it) {
    // For each value, if the server sends a NUL-separated
    // list of values, we separate that back out into
    // individual headers for each value in the list.
    // e.g.
    //    Set-Cookie "foo\0bar"
    // becomes
    //    Set-Cookie: foo\0
    //    Set-Cookie: bar\0
    SpdyString value = it->second.as_string();
    size_t start = 0;
    size_t end = 0;
    do {
      end = value.find('\0', start);
      SpdyString tval;
      if (end != value.npos)
        tval = value.substr(start, (end - start));
      else
        tval = value.substr(start);
      if (it->first[0] == ':')
        raw_headers.append(it->first.as_string().substr(1));
      else
        raw_headers.append(it->first.as_string());
      raw_headers.push_back(':');
      raw_headers.append(tval);
      raw_headers.push_back('\0');
      start = end + 1;
    } while (end != value.npos);
  }

  response->headers = new HttpResponseHeaders(raw_headers);
  response->was_fetched_via_spdy = true;
  return true;
}

void CreateSpdyHeadersFromHttpRequest(const HttpRequestInfo& info,
                                      const HttpRequestHeaders& request_headers,
                                      bool direct,
                                      SpdyHeaderBlock* headers) {
  (*headers)[kHttp2MethodHeader] = info.method;
  if (info.method == "CONNECT") {
    (*headers)[kHttp2AuthorityHeader] = GetHostAndPort(info.url);
  } else {
    (*headers)[kHttp2AuthorityHeader] = GetHostAndOptionalPort(info.url);
    (*headers)[kHttp2SchemeHeader] = info.url.scheme();
    (*headers)[kHttp2PathHeader] = info.url.PathForRequest();
  }

  HttpRequestHeaders::Iterator it(request_headers);
  while (it.GetNext()) {
    SpdyString name = base::ToLowerASCII(it.name());
    if (name.empty() || name[0] == ':' || name == "connection" ||
        name == "proxy-connection" || name == "transfer-encoding" ||
        name == "host") {
      continue;
    }
    AddSpdyHeader(name, it.value(), headers);
  }
}

static_assert(HIGHEST - LOWEST < 4 && HIGHEST - MINIMUM_PRIORITY < 6,
              "request priority incompatible with spdy");

SpdyPriority ConvertRequestPriorityToSpdyPriority(
    const RequestPriority priority) {
  DCHECK_GE(priority, MINIMUM_PRIORITY);
  DCHECK_LE(priority, MAXIMUM_PRIORITY);
  return static_cast<SpdyPriority>(MAXIMUM_PRIORITY - priority +
                                   kV3HighestPriority);
}

NET_EXPORT_PRIVATE RequestPriority
ConvertSpdyPriorityToRequestPriority(SpdyPriority priority) {
  // Handle invalid values gracefully.
  return ((priority - kV3HighestPriority) >
          (MAXIMUM_PRIORITY - MINIMUM_PRIORITY))
             ? IDLE
             : static_cast<RequestPriority>(MAXIMUM_PRIORITY -
                                            (priority - kV3HighestPriority));
}

NET_EXPORT_PRIVATE void ConvertHeaderBlockToHttpRequestHeaders(
    const SpdyHeaderBlock& spdy_headers,
    HttpRequestHeaders* http_headers) {
  for (const auto& it : spdy_headers) {
    SpdyStringPiece key = it.first;
    if (key[0] == ':') {
      key.remove_prefix(1);
    }
    std::vector<SpdyStringPiece> values = base::SplitStringPiece(
        it.second, "\0", base::TRIM_WHITESPACE, base::SPLIT_WANT_ALL);
    for (const auto& value : values) {
      http_headers->SetHeader(key, value);
    }
  }
}

GURL GetUrlFromHeaderBlock(const SpdyHeaderBlock& headers) {
  SpdyHeaderBlock::const_iterator it = headers.find(kHttp2SchemeHeader);
  if (it == headers.end())
    return GURL();
  SpdyString url = it->second.as_string();
  url.append("://");

  it = headers.find(kHttp2AuthorityHeader);
  if (it == headers.end())
    return GURL();
  url.append(it->second.as_string());

  it = headers.find(kHttp2PathHeader);
  if (it == headers.end())
    return GURL();
  url.append(it->second.as_string());
  return GURL(url);
}

}  // namespace net
