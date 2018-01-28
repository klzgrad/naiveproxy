// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef NET_HTTP2_TOOLS_RANDOM_UTIL_H_
#define NET_HTTP2_TOOLS_RANDOM_UTIL_H_

#include <stddef.h>

#include "net/http2/platform/api/http2_string.h"
#include "net/http2/platform/api/http2_string_piece.h"

namespace net {
namespace test {

class RandomBase;

// Returns a random string of length |len|, each character drawn uniformly and
// independently fom |alphabet|.
Http2String RandomString(RandomBase* rng, int len, Http2StringPiece alphabet);

// Returns a random integer in the range [lo, hi).
size_t GenerateUniformInRange(size_t lo, size_t hi, RandomBase* rng);

// Generate a string with the allowed character set for HTTP/2 / HPACK header
// names.
Http2String GenerateHttp2HeaderName(size_t len, RandomBase* rng);

// Generate a string with the web-safe string character set of specified len.
Http2String GenerateWebSafeString(size_t len, RandomBase* rng);

// Generate a string with the web-safe string character set of length [lo, hi).
Http2String GenerateWebSafeString(size_t lo, size_t hi, RandomBase* rng);

// Returns a random integer in the range [0, max], with a bias towards producing
// lower numbers.
size_t GenerateRandomSizeSkewedLow(size_t max, RandomBase* rng);

}  // namespace test
}  // namespace net

#endif  // NET_HTTP2_TOOLS_RANDOM_UTIL_H_
