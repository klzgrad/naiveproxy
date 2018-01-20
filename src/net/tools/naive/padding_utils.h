// Copyright 2020 klzgrad <kizdiv@gmail.com>. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
#ifndef NET_TOOLS_NAIVE_PADDING_UTILS_H_
#define NET_TOOLS_NAIVE_PADDING_UTILS_H_

#include <cstdint>

#include "base/containers/span.h"

namespace net {

void InitializeNonindexCodes();

// |unique_bits| SHOULD have relatively unique values.
void FillNonindexHeaderValue(uint64_t unique_bits, base::span<uint8_t> span);
}  // namespace net

#endif  // NET_TOOLS_NAIVE_PADDING_UTILS_H_
