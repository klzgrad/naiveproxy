// Copyright (c) 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/third_party/quic/platform/api/quic_url.h"
#include "net/third_party/quic/platform/api/quic_string.h"

namespace quic {

QuicUrl::QuicUrl(QuicStringPiece url) : impl_(url) {}

QuicUrl::QuicUrl(QuicStringPiece url, QuicStringPiece default_scheme)
    : impl_(url, default_scheme) {}

QuicUrl::QuicUrl(const QuicUrl& url) : impl_(url.impl()) {}

bool QuicUrl::IsValid() const {
  return impl_.IsValid();
}

QuicString QuicUrl::ToString() const {
  return impl_.ToStringIfValid();
}

QuicString QuicUrl::HostPort() const {
  return impl_.HostPort();
}

QuicString QuicUrl::PathParamsQuery() const {
  return impl_.PathParamsQuery();
}

QuicString QuicUrl::host() const {
  return impl_.host();
}

QuicString QuicUrl::path() const {
  return impl_.path();
}

QuicString QuicUrl::scheme() const {
  return impl_.scheme();
}

uint16_t QuicUrl::port() const {
  return impl_.port();
}

}  // namespace quic
