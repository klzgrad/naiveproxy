// Copyright 2020 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef QUICHE_COMMON_PLATFORM_API_QUICHE_TIME_UTILS_H_
#define QUICHE_COMMON_PLATFORM_API_QUICHE_TIME_UTILS_H_

#include <cstdint>

#include "quiche_platform_impl/quiche_time_utils_impl.h"

namespace quiche {

// Converts a civil time specified in UTC into a number of seconds since the
// Unix epoch.  This function is strict about validity of accepted dates.  For
// instance, it will reject February 29 on non-leap years, or 25 hours in a day.
// As a notable exception, 60 seconds is accepted to deal with potential leap
// seconds.  If the date predates Unix epoch, nullopt will be returned.
inline std::optional<int64_t> QuicheUtcDateTimeToUnixSeconds(
    int year, int month, int day, int hour, int minute, int second) {
  return QuicheUtcDateTimeToUnixSecondsImpl(year, month, day, hour, minute,
                                            second);
}

}  // namespace quiche

#endif  // QUICHE_COMMON_PLATFORM_API_QUICHE_TIME_UTILS_H_
