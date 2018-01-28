// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef NET_HTTP2_HTTP2_CONSTANTS_TEST_UTIL_H_
#define NET_HTTP2_HTTP2_CONSTANTS_TEST_UTIL_H_

#include <vector>

#include "net/http2/http2_constants.h"

namespace net {
namespace test {

// Returns a vector of all supported frame types.
std::vector<Http2FrameType> AllHttp2FrameTypes();

// Returns a vector of all supported frame flags for the specified
// frame type. Empty if the type is unknown.
std::vector<Http2FrameFlag> AllHttp2FrameFlagsForFrameType(Http2FrameType type);

// Returns a vector of all supported RST_STREAM and GOAWAY error codes.
std::vector<Http2ErrorCode> AllHttp2ErrorCodes();

// Returns a vector of all supported parameters in SETTINGS frames.
std::vector<Http2SettingsParameter> AllHttp2SettingsParameters();

// Returns a mask of flags supported for the specified frame type. Returns
// zero for unknown frame types.
uint8_t KnownFlagsMaskForFrameType(Http2FrameType type);

// Returns a mask of flag bits known to be invalid for the frame type.
// For unknown frame types, the mask is zero; i.e., we don't know that any
// are invalid.
uint8_t InvalidFlagMaskForFrameType(Http2FrameType type);

}  // namespace test
}  // namespace net

#endif  // NET_HTTP2_HTTP2_CONSTANTS_TEST_UTIL_H_
