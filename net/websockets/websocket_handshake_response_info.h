// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef NET_WEBSOCKETS_WEBSOCKET_HANDSHAKE_RESPONSE_INFO_H_
#define NET_WEBSOCKETS_WEBSOCKET_HANDSHAKE_RESPONSE_INFO_H_

#include <string>

#include "base/macros.h"
#include "base/memory/ref_counted.h"
#include "base/time/time.h"
#include "net/base/net_export.h"
#include "url/gurl.h"

namespace net {

class HttpResponseHeaders;

struct NET_EXPORT WebSocketHandshakeResponseInfo {
  WebSocketHandshakeResponseInfo(const GURL& url,
                                 int status_code,
                                 const std::string& status_text,
                                 scoped_refptr<HttpResponseHeaders> headers,
                                 base::Time response_time);
  ~WebSocketHandshakeResponseInfo();
  // The request URL
  GURL url;
  // HTTP status code
  int status_code;
  // HTTP status text
  std::string status_text;
  // HTTP response headers
  scoped_refptr<HttpResponseHeaders> headers;
  // The time that this response arrived
  base::Time response_time;

 private:
  DISALLOW_COPY_AND_ASSIGN(WebSocketHandshakeResponseInfo);
};

}  // namespace net

#endif  // NET_WEBSOCKETS_WEBSOCKET_HANDSHAKE_RESPONSE_INFO_H_
