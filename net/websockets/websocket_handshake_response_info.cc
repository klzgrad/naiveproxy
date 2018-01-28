// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/websockets/websocket_handshake_response_info.h"

#include <string>

#include "base/memory/ref_counted.h"
#include "base/time/time.h"
#include "net/http/http_response_headers.h"
#include "url/gurl.h"

namespace net {

WebSocketHandshakeResponseInfo::WebSocketHandshakeResponseInfo(
    const GURL& url,
    int status_code,
    const std::string& status_text,
    scoped_refptr<HttpResponseHeaders> headers,
    base::Time response_time)
    : url(url),
      status_code(status_code),
      status_text(status_text),
      headers(headers),
      response_time(response_time) {}

WebSocketHandshakeResponseInfo::~WebSocketHandshakeResponseInfo() = default;

}  // namespace net
