// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef QUICHE_HTTP2_TOOLS_RANDOM_UTIL_H_
#define QUICHE_HTTP2_TOOLS_RANDOM_UTIL_H_

#include <stddef.h>

#include <string>

#include "net/third_party/quiche/src/http2/test_tools/http2_random.h"

namespace http2 {
namespace test {

// Generate a string with the allowed character set for HTTP/2 / HPACK header
// names.
std::string GenerateHttp2HeaderName(size_t len, Http2Random* rng);

// Generate a string with the web-safe string character set of specified len.
std::string GenerateWebSafeString(size_t len, Http2Random* rng);

// Generate a string with the web-safe string character set of length [lo, hi).
std::string GenerateWebSafeString(size_t lo, size_t hi, Http2Random* rng);

}  // namespace test
}  // namespace http2

#endif  // QUICHE_HTTP2_TOOLS_RANDOM_UTIL_H_
