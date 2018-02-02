// Copyright (c) 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef NET_QUIC_PLATFORM_API_QUIC_URL_H_
#define NET_QUIC_PLATFORM_API_QUIC_URL_H_

#include "net/quic/platform/api/quic_export.h"
#include "net/quic/platform/api/quic_string_piece.h"
#include "net/quic/platform/impl/quic_url_impl.h"

namespace net {

// QuicUrl stores a representation of a URL.
class QUIC_EXPORT_PRIVATE QuicUrl {
 public:
  // Constructs an empty QuicUrl.
  QuicUrl() = default;

  // Constructs a QuicUrl from the url string |url|.
  // NOTE: If |url| doesn't have a scheme, it will have an empty scheme
  // field. If that's not what you want, use the QuicUrlImpl(url,
  // default_scheme) form below.
  explicit QuicUrl(QuicStringPiece url);

  // Constructs a QuicUrl from |url|, assuming that the scheme for the QuicUrl
  // is |default_scheme| if there is no scheme specified in |url|.
  QuicUrl(QuicStringPiece url, QuicStringPiece default_scheme);

  QuicUrl(const QuicUrl& url);

  // Returns false if any of these conditions occur:
  // No scheme specified
  // Host name too long (the maximum hostname length is platform-dependent)
  // Invalid characters in host name, path or params
  // Invalid port number (e.g. greater than 65535)
  bool IsValid() const;

  // PLEASE NOTE: ToString(), HostPort(), PathParamsQuery(), scheme(), host(),
  // path() and port() functions should be only called on a valid QuicUrl.
  // Return values are platform-dependent if called on a invalid QuicUrl.

  // Returns full text of the QuicUrl.
  std::string ToString() const;

  // Returns host:port.
  // If the host is empty, it will return an empty string.
  // If the host is an IPv6 address, it will be bracketed.
  // If port is not present or is equal to default_port of scheme (e.g., port
  // 80 for HTTP), it won't be returned.
  std::string HostPort() const;

  // Returns a string assembles path, parameters and query.
  std::string PathParamsQuery() const;

  std::string scheme() const;
  std::string host() const;
  std::string path() const;
  uint16_t port() const;

  const QuicUrlImpl& impl() const { return impl_; }

 private:
  QuicUrlImpl impl_;
};

}  // namespace net

#endif  // NET_QUIC_PLATFORM_API_QUIC_URL_H_
