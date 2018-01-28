// Copyright (c) 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/quic/platform/impl/quic_url_impl.h"

#include "net/quic/platform/api/quic_text_utils.h"

using std::string;

namespace net {

QuicUrlImpl::QuicUrlImpl(QuicStringPiece url) : url_(url) {}

QuicUrlImpl::QuicUrlImpl(QuicStringPiece url, QuicStringPiece default_scheme)
    : url_(url) {
  if (url_.has_scheme()) {
    return;
  }
  string buffer = default_scheme.as_string() + "://" + url.as_string();
  url_ = GURL(buffer);
}

QuicUrlImpl::QuicUrlImpl(const QuicUrlImpl& url) : url_(url.url()) {}

string QuicUrlImpl::ToStringIfValid() const {
  if (IsValid()) {
    return url_.spec();
  }
  return "";
}

bool QuicUrlImpl::IsValid() const {
  if (!url_.is_valid() || !url_.has_scheme()) {
    return false;
  }

  if (url_.has_host() && url_.host().length() > kMaxHostNameLength) {
    return false;
  }

  return true;
}

string QuicUrlImpl::HostPort() const {
  if (!IsValid() || !url_.has_host()) {
    return "";
  }

  string buffer = url_.host();
  int port = url_.IntPort();
  string scheme = url_.scheme();
  if (port == url::PORT_UNSPECIFIED ||
      (url_.IsStandard() &&
       port == url::DefaultPortForScheme(scheme.c_str(), scheme.length()))) {
    return buffer;
  }
  buffer = buffer + ":" + std::to_string(port);
  return buffer;
}

string QuicUrlImpl::PathParamsQuery() const {
  if (!IsValid() || !url_.has_path()) {
    return "/";
  }

  return url_.PathForRequest();
}

string QuicUrlImpl::scheme() const {
  if (!IsValid()) {
    return "";
  }

  return url_.scheme();
}

string QuicUrlImpl::host() const {
  if (!IsValid()) {
    return "";
  }

  return url_.HostNoBrackets();
}

string QuicUrlImpl::path() const {
  if (!IsValid()) {
    return "";
  }

  return url_.path();
}

uint16_t QuicUrlImpl::port() const {
  if (!IsValid()) {
    return 0;
  }

  int port = url_.EffectiveIntPort();
  if (port == url::PORT_UNSPECIFIED) {
    return 0;
  }
  return port;
}

}  // namespace net
