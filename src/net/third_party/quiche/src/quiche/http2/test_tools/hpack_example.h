// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef QUICHE_HTTP2_TEST_TOOLS_HPACK_EXAMPLE_H_
#define QUICHE_HTTP2_TEST_TOOLS_HPACK_EXAMPLE_H_

#include <string>

#include "absl/strings/string_view.h"

// Parses HPACK examples in the format seen in the HPACK specification,
// RFC 7541. For example:
//
//       10                                      | == Literal never indexed ==
//       08                                      |   Literal name (len = 8)
//       7061 7373 776f 7264                     | password
//       06                                      |   Literal value (len = 6)
//       7365 6372 6574                          | secret
//                                               | -> password: secret
//
// (excluding the leading "//").

namespace http2 {
namespace test {

std::string HpackExampleToStringOrDie(absl::string_view example);

}  // namespace test
}  // namespace http2

#endif  // QUICHE_HTTP2_TEST_TOOLS_HPACK_EXAMPLE_H_
