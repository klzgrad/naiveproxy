// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef NET_SPDY_CHROMIUM_SPDY_HTTP_UTILS_H_
#define NET_SPDY_CHROMIUM_SPDY_HTTP_UTILS_H_

#include "net/base/net_export.h"
#include "net/base/request_priority.h"
#include "net/spdy/core/spdy_framer.h"
#include "net/spdy/core/spdy_header_block.h"
#include "net/spdy/core/spdy_protocol.h"
#include "url/gurl.h"

namespace net {

class HttpResponseInfo;
struct HttpRequestInfo;
class HttpRequestHeaders;

// Convert a SpdyHeaderBlock into an HttpResponseInfo.
// |headers| input parameter with the SpdyHeaderBlock.
// |response| output parameter for the HttpResponseInfo.
// Returns true if successfully converted.  False if the SpdyHeaderBlock is
// incomplete (e.g. missing 'status' or 'version').
NET_EXPORT bool SpdyHeadersToHttpResponse(
    const SpdyHeaderBlock& headers,
    HttpResponseInfo* response);

// Create a SpdyHeaderBlock from HttpRequestInfo and HttpRequestHeaders.
NET_EXPORT void CreateSpdyHeadersFromHttpRequest(
    const HttpRequestInfo& info,
    const HttpRequestHeaders& request_headers,
    bool direct,
    SpdyHeaderBlock* headers);

// Create HttpRequestHeaders from SpdyHeaderBlock.
NET_EXPORT void ConvertHeaderBlockToHttpRequestHeaders(
    const SpdyHeaderBlock& spdy_headers,
    HttpRequestHeaders* http_headers);

// Returns the URL associated with the |headers| by assembling the
// scheme, host and path from the protocol specific keys.
NET_EXPORT GURL GetUrlFromHeaderBlock(const SpdyHeaderBlock& headers);

NET_EXPORT SpdyPriority
ConvertRequestPriorityToSpdyPriority(RequestPriority priority);

NET_EXPORT RequestPriority
ConvertSpdyPriorityToRequestPriority(SpdyPriority priority);

}  // namespace net

#endif  // NET_SPDY_CHROMIUM_SPDY_HTTP_UTILS_H_
