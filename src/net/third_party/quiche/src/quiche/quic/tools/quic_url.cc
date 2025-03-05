// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "quiche/quic/tools/quic_url.h"

#include <string>

#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"

namespace quic {

static constexpr size_t kMaxHostNameLength = 256;

QuicUrl::QuicUrl(absl::string_view url) : url_(static_cast<std::string>(url)) {}

QuicUrl::QuicUrl(absl::string_view url, absl::string_view default_scheme)
    : QuicUrl(url) {
  if (url_.has_scheme()) {
    return;
  }

  url_ = GURL(absl::StrCat(default_scheme, "://", url));
}

std::string QuicUrl::ToString() const {
  if (IsValid()) {
    return url_.spec();
  }
  return "";
}

bool QuicUrl::IsValid() const {
  if (!url_.is_valid() || !url_.has_scheme()) {
    return false;
  }

  if (url_.has_host() && url_.host().length() > kMaxHostNameLength) {
    return false;
  }

  return true;
}

std::string QuicUrl::HostPort() const {
  if (!IsValid() || !url_.has_host()) {
    return "";
  }

  std::string host = url_.host();
  int port = url_.IntPort();
  if (port == url::PORT_UNSPECIFIED) {
    return host;
  }
  return absl::StrCat(host, ":", port);
}

std::string QuicUrl::PathParamsQuery() const {
  if (!IsValid() || !url_.has_path()) {
    return "/";
  }

  return url_.PathForRequest();
}

std::string QuicUrl::scheme() const {
  if (!IsValid()) {
    return "";
  }

  return url_.scheme();
}

std::string QuicUrl::host() const {
  if (!IsValid()) {
    return "";
  }

  return url_.HostNoBrackets();
}

std::string QuicUrl::path() const {
  if (!IsValid()) {
    return "";
  }

  return url_.path();
}

uint16_t QuicUrl::port() const {
  if (!IsValid()) {
    return 0;
  }

  int port = url_.EffectiveIntPort();
  if (port == url::PORT_UNSPECIFIED) {
    return 0;
  }
  return port;
}

}  // namespace quic
