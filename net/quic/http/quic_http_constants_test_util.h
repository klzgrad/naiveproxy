// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef NET_QUIC_HTTP_QUIC_HTTP_CONSTANTS_TEST_UTIL_H_
#define NET_QUIC_HTTP_QUIC_HTTP_CONSTANTS_TEST_UTIL_H_

#include <cstdint>
#include <vector>

#include "net/quic/http/quic_http_constants.h"

namespace net {
namespace test {

// Returns a vector of all supported frame types.
std::vector<QuicHttpFrameType> AllQuicHttpFrameTypes();

// Returns a vector of all supported frame flags for the specified
// frame type. Empty if the type is unknown.
std::vector<QuicHttpFrameFlag> AllQuicHttpFrameFlagsForFrameType(
    QuicHttpFrameType type);

// Returns a vector of all supported RST_STREAM and GOAWAY error codes.
std::vector<QuicHttpErrorCode> AllQuicHttpErrorCodes();

// Returns a vector of all supported parameters in SETTINGS frames.
std::vector<QuicHttpSettingsParameter> AllQuicHttpSettingsParameters();

// Returns a mask of flags supported for the specified frame type. Returns
// zero for unknown frame types.
uint8_t KnownFlagsMaskForFrameType(QuicHttpFrameType type);

// Returns a mask of flag bits known to be invalid for the frame type.
// For unknown frame types, the mask is zero; i.e., we don't know that any
// are invalid.
uint8_t InvalidFlagMaskForFrameType(QuicHttpFrameType type);

}  // namespace test
}  // namespace net

#endif  // NET_QUIC_HTTP_QUIC_HTTP_CONSTANTS_TEST_UTIL_H_
