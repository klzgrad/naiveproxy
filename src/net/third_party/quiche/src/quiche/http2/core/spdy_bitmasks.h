// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef QUICHE_HTTP2_CORE_SPDY_BITMASKS_H_
#define QUICHE_HTTP2_CORE_SPDY_BITMASKS_H_

namespace spdy {

// StreamId mask from the SpdyHeader
inline constexpr unsigned int kStreamIdMask = 0x7fffffff;

// Mask the lower 24 bits.
inline constexpr unsigned int kLengthMask = 0xffffff;

}  // namespace spdy

#endif  // QUICHE_HTTP2_CORE_SPDY_BITMASKS_H_
