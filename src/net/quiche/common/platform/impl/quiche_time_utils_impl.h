// Copyright 2020 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef NET_QUICHE_COMMON_PLATFORM_IMPL_QUICHE_TIME_UTILS_IMPL_H_
#define NET_QUICHE_COMMON_PLATFORM_IMPL_QUICHE_TIME_UTILS_IMPL_H_

#include <cstdint>

#include "net/base/net_export.h"
#include "net/third_party/quiche/src/common/platform/api/quiche_optional.h"

namespace quiche {

NET_EXPORT_PRIVATE QuicheOptional<int64_t> QuicheUtcDateTimeToUnixSecondsImpl(
    int year,
    int month,
    int day,
    int hour,
    int minute,
    int second);

}  // namespace quiche

#endif  // NET_QUICHE_COMMON_PLATFORM_IMPL_QUICHE_TIME_UTILS_IMPL_H_
