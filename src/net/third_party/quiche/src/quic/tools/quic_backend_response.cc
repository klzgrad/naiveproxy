// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/third_party/quiche/src/quic/tools/quic_backend_response.h"

namespace quic {

QuicBackendResponse::ServerPushInfo::ServerPushInfo(
    QuicUrl request_url,
    spdy::SpdyHeaderBlock headers,
    spdy::SpdyPriority priority,
    std::string body)
    : request_url(request_url),
      headers(std::move(headers)),
      priority(priority),
      body(body) {}

QuicBackendResponse::ServerPushInfo::ServerPushInfo(const ServerPushInfo& other)
    : request_url(other.request_url),
      headers(other.headers.Clone()),
      priority(other.priority),
      body(other.body) {}

QuicBackendResponse::QuicBackendResponse() : response_type_(REGULAR_RESPONSE) {}

QuicBackendResponse::~QuicBackendResponse() = default;

}  // namespace quic
