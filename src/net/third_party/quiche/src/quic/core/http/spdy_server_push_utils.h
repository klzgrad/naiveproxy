// Copyright (c) 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef QUICHE_QUIC_CORE_HTTP_SPDY_SERVER_PUSH_UTILS_H_
#define QUICHE_QUIC_CORE_HTTP_SPDY_SERVER_PUSH_UTILS_H_

#include "net/third_party/quiche/src/quic/platform/api/quic_export.h"
#include "net/third_party/quiche/src/common/platform/api/quiche_string_piece.h"
#include "net/third_party/quiche/src/spdy/core/spdy_header_block.h"

namespace quic {

class QUIC_EXPORT_PRIVATE SpdyServerPushUtils {
 public:
  SpdyServerPushUtils() = delete;

  // Returns a canonicalized URL composed from the :scheme, :authority, and
  // :path headers of a PUSH_PROMISE. Returns empty string if the headers do not
  // conform to HTTP/2 spec or if the ":method" header contains a forbidden
  // method for PUSH_PROMISE.
  static std::string GetPromisedUrlFromHeaders(
      const spdy::SpdyHeaderBlock& headers);

  // Returns hostname, or empty string if missing.
  static std::string GetPromisedHostNameFromHeaders(
      const spdy::SpdyHeaderBlock& headers);

  // Returns true if result of |GetPromisedUrlFromHeaders()| is non-empty
  // and is a well-formed URL.
  static bool PromisedUrlIsValid(const spdy::SpdyHeaderBlock& headers);

  // Returns a canonical, valid URL for a PUSH_PROMISE with the specified
  // ":scheme", ":authority", and ":path" header fields, or an empty
  // string if the resulting URL is not valid or supported.
  static std::string GetPushPromiseUrl(quiche::QuicheStringPiece scheme,
                                       quiche::QuicheStringPiece authority,
                                       quiche::QuicheStringPiece path);
};

}  // namespace quic

#endif  // QUICHE_QUIC_CORE_HTTP_SPDY_SERVER_PUSH_UTILS_H_
