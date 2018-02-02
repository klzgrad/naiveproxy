// Copyright (c) 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef NET_QUIC_CORE_SPDY_UTILS_H_
#define NET_QUIC_CORE_SPDY_UTILS_H_

#include <cstddef>
#include <cstdint>
#include <string>

#include "base/macros.h"
#include "net/quic/core/quic_header_list.h"
#include "net/quic/core/quic_packets.h"
#include "net/quic/platform/api/quic_export.h"
#include "net/spdy/core/spdy_framer.h"

namespace net {

class QUIC_EXPORT_PRIVATE SpdyUtils {
 public:
  // Populate |content length| with the value of the content-length header.
  // Returns true on success, false if parsing fails or content-length header is
  // missing.
  static bool ExtractContentLengthFromHeaders(int64_t* content_length,
                                              SpdyHeaderBlock* headers);

  // Copies a list of headers to a SpdyHeaderBlock.
  static bool CopyAndValidateHeaders(const QuicHeaderList& header_list,
                                     int64_t* content_length,
                                     SpdyHeaderBlock* headers);

  // Copies a list of headers to a SpdyHeaderBlock.
  static bool CopyAndValidateTrailers(const QuicHeaderList& header_list,
                                      size_t* final_byte_offset,
                                      SpdyHeaderBlock* trailers);

  // Returns URL composed from scheme, authority, and path header
  // values, or empty string if any of those fields are missing.
  static std::string GetUrlFromHeaderBlock(const SpdyHeaderBlock& headers);

  // Returns hostname, or empty string if missing.
  static std::string GetHostNameFromHeaderBlock(const SpdyHeaderBlock& headers);

  // Returns true if result of |GetUrlFromHeaderBlock()| is non-empty
  // and is a well-formed URL.
  static bool UrlIsValid(const SpdyHeaderBlock& headers);

  // Populates the fields of |headers| to make a GET request of |url|,
  // which must be fully-qualified.
  static bool PopulateHeaderBlockFromUrl(const std::string url,
                                         SpdyHeaderBlock* headers);

 private:
  DISALLOW_COPY_AND_ASSIGN(SpdyUtils);
};

}  // namespace net

#endif  // NET_QUIC_CORE_SPDY_UTILS_H_
