// Copyright (c) 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/quic/platform/api/quic_url.h"

using std::string;

namespace net {

QuicUrl::QuicUrl(QuicStringPiece url) : impl_(url) {}

QuicUrl::QuicUrl(QuicStringPiece url, QuicStringPiece default_scheme)
    : impl_(url, default_scheme) {}

QuicUrl::QuicUrl(const QuicUrl& url) : impl_(url.impl()) {}

bool QuicUrl::IsValid() const {
  return impl_.IsValid();
}

string QuicUrl::ToString() const {
  return impl_.ToStringIfValid();
}

string QuicUrl::HostPort() const {
  return impl_.HostPort();
}

string QuicUrl::PathParamsQuery() const {
  return impl_.PathParamsQuery();
}

string QuicUrl::host() const {
  return impl_.host();
}

string QuicUrl::path() const {
  return impl_.path();
}

string QuicUrl::scheme() const {
  return impl_.scheme();
}

uint16_t QuicUrl::port() const {
  return impl_.port();
}

}  // namespace net
