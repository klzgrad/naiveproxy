// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef QUICHE_SPDY_PLATFORM_API_SPDY_STRING_UTILS_H_
#define QUICHE_SPDY_PLATFORM_API_SPDY_STRING_UTILS_H_

#include <string>

#include "absl/strings/string_view.h"
#include "net/spdy/platform/impl/spdy_string_utils_impl.h"

namespace spdy {

inline std::string SpdyHexDump(absl::string_view data) {
  return SpdyHexDumpImpl(data);
}

}  // namespace spdy

#endif  // QUICHE_SPDY_PLATFORM_API_SPDY_STRING_UTILS_H_
