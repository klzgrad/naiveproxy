// Copyright (c) 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// This file contains data structures and utility functions used for serializing
// and parsing alternative service header values, common to HTTP/1.1 header
// fields and HTTP/2 and QUIC ALTSVC frames.  See specification at
// https://httpwg.github.io/http-extensions/alt-svc.html.

#ifndef QUICHE_HTTP2_CORE_SPDY_ALT_SVC_WIRE_FORMAT_H_
#define QUICHE_HTTP2_CORE_SPDY_ALT_SVC_WIRE_FORMAT_H_

#include <cstdint>
#include <string>
#include <vector>

#include "absl/container/inlined_vector.h"
#include "absl/strings/string_view.h"
#include "quiche/common/platform/api/quiche_export.h"

namespace spdy {

namespace test {
class SpdyAltSvcWireFormatPeer;
}  // namespace test

class QUICHE_EXPORT SpdyAltSvcWireFormat {
 public:
  using VersionVector = absl::InlinedVector<uint32_t, 8>;

  struct QUICHE_EXPORT AlternativeService {
    std::string protocol_id;
    std::string host;

    // Default is 0: invalid port.
    uint16_t port = 0;
    // Default is one day.
    uint32_t max_age_seconds = 86400;
    // Default is empty: unspecified version.
    VersionVector version;

    AlternativeService();
    AlternativeService(const std::string& protocol_id, const std::string& host,
                       uint16_t port, uint32_t max_age_seconds,
                       VersionVector version);
    AlternativeService(const AlternativeService& other);
    ~AlternativeService();

    bool operator==(const AlternativeService& other) const {
      return protocol_id == other.protocol_id && host == other.host &&
             port == other.port && version == other.version &&
             max_age_seconds == other.max_age_seconds;
    }
  };
  // An empty vector means alternative services should be cleared for given
  // origin.  Note that the wire format for this is the string "clear", not an
  // empty value (which is invalid).
  typedef std::vector<AlternativeService> AlternativeServiceVector;

  friend class test::SpdyAltSvcWireFormatPeer;
  static bool ParseHeaderFieldValue(absl::string_view value,
                                    AlternativeServiceVector* altsvc_vector);
  static std::string SerializeHeaderFieldValue(
      const AlternativeServiceVector& altsvc_vector);

 private:
  // Forward |*c| over space and tab or until |end| is reached.
  static void SkipWhiteSpace(absl::string_view::const_iterator* c,
                             absl::string_view::const_iterator end);
  // Decode percent-decoded string between |c| and |end| into |*output|.
  // Return true on success, false if input is invalid.
  static bool PercentDecode(absl::string_view::const_iterator c,
                            absl::string_view::const_iterator end,
                            std::string* output);
  // Parse the authority part of Alt-Svc between |c| and |end| into |*host| and
  // |*port|.  Return true on success, false if input is invalid.
  static bool ParseAltAuthority(absl::string_view::const_iterator c,
                                absl::string_view::const_iterator end,
                                std::string* host, uint16_t* port);
  // Parse a positive integer between |c| and |end| into |*value|.
  // Return true on success, false if input is not a positive integer or it
  // cannot be represented on uint16_t.
  static bool ParsePositiveInteger16(absl::string_view::const_iterator c,
                                     absl::string_view::const_iterator end,
                                     uint16_t* value);
  // Parse a positive integer between |c| and |end| into |*value|.
  // Return true on success, false if input is not a positive integer or it
  // cannot be represented on uint32_t.
  static bool ParsePositiveInteger32(absl::string_view::const_iterator c,
                                     absl::string_view::const_iterator end,
                                     uint32_t* value);
  // Parse |c| as hexadecimal digit, case insensitive.  |c| must be [0-9a-fA-F].
  // Output is between 0 and 15.
  static char HexDigitToInt(char c);
  // Parse |data| as hexadecimal number into |*value|.  |data| must only contain
  // hexadecimal digits, no "0x" prefix.
  // Return true on success, false if input is empty, not valid hexadecimal
  // number, or cannot be represented on uint32_t.
  static bool HexDecodeToUInt32(absl::string_view data, uint32_t* value);
};

}  // namespace spdy

#endif  // QUICHE_HTTP2_CORE_SPDY_ALT_SVC_WIRE_FORMAT_H_
