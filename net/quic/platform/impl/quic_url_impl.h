// Copyright (c) 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef NET_QUIC_PLATFORM_IMPL_QUIC_URL_IMPL_H_
#define NET_QUIC_PLATFORM_IMPL_QUIC_URL_IMPL_H_

#include "net/quic/platform/api/quic_export.h"
#include "net/quic/platform/api/quic_string_piece.h"
#include "url/gurl.h"

namespace net {

class QUIC_EXPORT_PRIVATE QuicUrlImpl {
 public:
  static const size_t kMaxHostNameLength = 256;

  // Constructs an empty QuicUrl.
  QuicUrlImpl() = default;

  // Constructs a QuicUrlImpl from the url string |url|.
  // NOTE: If |url| doesn't have a scheme, it will have an empty scheme
  // field. If that's not what you want, use the QuicUrlImpl(url,
  // default_scheme) form below.
  explicit QuicUrlImpl(QuicStringPiece url);

  // Constructs a QuicUrlImpl from |url|, assuming that the scheme for the URL
  // is |default_scheme| if there is no scheme specified in |url|.
  QuicUrlImpl(QuicStringPiece url, QuicStringPiece default_scheme);

  QuicUrlImpl(const QuicUrlImpl& url);

  // Returns false if any of these conditions occur:
  // No scheme specified
  // Host name too long (> 256 bytes)
  // Invalid characters in host name, path or params
  // Invalid port number (e.g. greater than 65535)
  bool IsValid() const;

  // Returns full text of the QuicUrlImpl if it is valid. Return empty string
  // otherwise.
  std::string ToStringIfValid() const;

  // Returns host:port.
  // If the host is empty, it will return an empty std::string.
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

  const GURL& url() const { return url_; }

 private:
  GURL url_;
};

}  // namespace net

#endif  // NET_QUIC_PLATFORM_IMPL_QUIC_URL_IMPL_H_
