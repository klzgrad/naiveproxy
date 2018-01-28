// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef NET_QUIC_PLATFORM_IMPL_QUIC_STRING_UTILS_IMPL_H_
#define NET_QUIC_PLATFORM_IMPL_QUIC_STRING_UTILS_IMPL_H_

#include <utility>

#include "base/strings/string_number_conversions.h"
#include "base/strings/string_util.h"
#include "base/strings/stringprintf.h"
#include "net/base/hex_utils.h"
#include "net/quic/platform/api/quic_str_cat.h"
#include "net/quic/platform/api/quic_string.h"
#include "net/quic/platform/api/quic_string_piece.h"

namespace net {

template <typename... Args>
inline void QuicStrAppendImpl(QuicString* output, const Args&... args) {
  output->append(QuicStrCatImpl(args...));
}

}  // namespace net

#endif  // NET_QUIC_PLATFORM_IMPL_QUIC_STRING_UTILS_IMPL_H_
