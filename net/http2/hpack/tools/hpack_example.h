// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef NET_HTTP2_HPACK_TOOLS_HPACK_EXAMPLE_H_
#define NET_HTTP2_HPACK_TOOLS_HPACK_EXAMPLE_H_

#include "net/http2/platform/api/http2_string.h"
#include "net/http2/platform/api/http2_string_piece.h"

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

namespace net {
namespace test {

Http2String HpackExampleToStringOrDie(Http2StringPiece example);

}  // namespace test
}  // namespace net

#endif  // NET_HTTP2_HPACK_TOOLS_HPACK_EXAMPLE_H_
