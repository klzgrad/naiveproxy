// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef QUICHE_QUIC_TOOLS_QUIC_URL_H_
#define QUICHE_QUIC_TOOLS_QUIC_URL_H_

#include <string>

#include "url/gurl.h"
#include "net/third_party/quiche/src/quic/platform/api/quic_export.h"
#include "net/third_party/quiche/src/common/platform/api/quiche_string_piece.h"

namespace quic {

// A utility class that wraps GURL.
class QuicUrl {
 public:
  // Constructs an empty QuicUrl.
  QuicUrl() = default;

  // Constructs a QuicUrl from the url string |url|.
  //
  // NOTE: If |url| doesn't have a scheme, it will have an empty scheme
  // field. If that's not what you want, use the QuicUrlImpl(url,
  // default_scheme) form below.
  explicit QuicUrl(quiche::QuicheStringPiece url);

  // Constructs a QuicUrlImpl from |url|, assuming that the scheme for the URL
  // is |default_scheme| if there is no scheme specified in |url|.
  QuicUrl(quiche::QuicheStringPiece url,
          quiche::QuicheStringPiece default_scheme);

  // Returns false if the URL is not valid.
  bool IsValid() const;

  // Returns full text of the QuicUrl if it is valid. Return empty string
  // otherwise.
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

 private:
  GURL url_;
};

}  // namespace quic

#endif  // QUICHE_QUIC_TOOLS_QUIC_URL_H_
