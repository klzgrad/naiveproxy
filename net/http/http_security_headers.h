// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef NET_HTTP_HTTP_SECURITY_HEADERS_H_
#define NET_HTTP_HTTP_SECURITY_HEADERS_H_

#include <stdint.h>

#include <string>

#include "base/time/time.h"
#include "base/values.h"
#include "net/base/hash_value.h"
#include "net/base/net_export.h"

class GURL;

namespace net {

const uint32_t kMaxHSTSAgeSecs = 86400 * 365;  // 1 year

// RFC7469 suggests that 60 days is a reasonable maximum max-age value
// http://tools.ietf.org/html/rfc7469#section-4.1
const uint32_t kMaxHPKPAgeSecs = 86400 * 60;  // 60 days

// https://tools.ietf.org/html/draft-ietf-httpbis-expect-ct-00 suggests a
// conservative maximum max-age, at least while Expect-CT is new.
const uint32_t kMaxExpectCTAgeSecs = 86400 * 30;  // 30 days

// Parses |value| as a Strict-Transport-Security header value. If successful,
// returns true and sets |*max_age| and |*include_subdomains|.
// Otherwise returns false and leaves the output parameters unchanged.
//
// value is the right-hand side of:
//
// "Strict-Transport-Security" ":"
//     [ directive ]  *( ";" [ directive ] )
bool NET_EXPORT_PRIVATE ParseHSTSHeader(const std::string& value,
                                        base::TimeDelta* max_age,
                                        bool* include_subdomains);

// Parses |value| as a Public-Key-Pins header value. If successful, returns
// true and populates the |*max_age|, |*include_subdomains|, |*hashes|, and
// |*report_uri| values. Otherwise returns false and leaves the output
// parameters unchanged.
//
// value is the right-hand side of:
//
// "Public-Key-Pins" ":"
//     "max-age" "=" delta-seconds ";"
//     "pin-" algo "=" base64 [ ";" ... ]
//     [ ";" "includeSubdomains" ]
//     [ ";" "report-uri" "=" uri-reference ]
//
// For this function to return true, the key hashes specified by the HPKP
// header must pass two additional checks. There MUST be at least one key
// hash which matches the SSL certificate chain of the current site (as
// specified by the chain_hashes) parameter. In addition, there MUST be at
// least one key hash which does NOT match the site's SSL certificate chain
// (this is the "backup pin").
bool NET_EXPORT_PRIVATE ParseHPKPHeader(const std::string& value,
                                        const HashValueVector& chain_hashes,
                                        base::TimeDelta* max_age,
                                        bool* include_subdomains,
                                        HashValueVector* hashes,
                                        GURL* report_uri);

// Parses |value| as a Public-Key-Pins-Report-Only header value. If
// successful, returns true and populates the |*include_subdomains|,
// |*hashes|, and |*report_uri| values. Otherwise returns false and
// leaves the output parameters unchanged.
//
// value is the right-hand side of:
//
// "Public-Key-Pins-Report-Only" ":"
//     [ "max-age" "=" delta-seconds ";" ]
//     "pin-" algo "=" base64 [ ";" ... ]
//     [ ";" "includeSubdomains" ]
//     [ ";" "report-uri" "=" uri-reference ]
//
bool NET_EXPORT_PRIVATE ParseHPKPReportOnlyHeader(const std::string& value,
                                                  bool* include_subdomains,
                                                  HashValueVector* hashes,
                                                  GURL* report_uri);

// Parses |value| as an Expect-CT header value. If successful, returns true and
// populates the |*max_age|, |*enforce|, and |*report_uri| values. Otherwise
// returns false and leaves the output parameters unchanged.
//
// |value| is the right-hand side of:
// "Expect-CT" ":"
//     "max-age" "=" delta-seconds
//     [ "," "enforce" ]
//     [ "," "report-uri" "=" uri-reference ]
//
bool NET_EXPORT_PRIVATE ParseExpectCTHeader(const std::string& value,
                                            base::TimeDelta* max_age,
                                            bool* enforce,
                                            GURL* report_uri);

}  // namespace net

#endif  // NET_HTTP_HTTP_SECURITY_HEADERS_H_
