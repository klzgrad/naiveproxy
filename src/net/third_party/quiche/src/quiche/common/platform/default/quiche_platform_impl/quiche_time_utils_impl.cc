// Copyright 2021 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "quiche_platform_impl/quiche_time_utils_impl.h"

#include <optional>

#include "openssl/time.h"

namespace quiche {

// Chrome converts broken out UTC times for certificates to unix times using
// the BoringSSL routines.
std::optional<int64_t> QuicheUtcDateTimeToUnixSecondsImpl(int year, int month,
                                                          int day, int hour,
                                                          int minute,
                                                          int second) {
  struct tm tmp_tm;
  tmp_tm.tm_year = year - 1900;
  tmp_tm.tm_mon = month - 1;
  tmp_tm.tm_mday = day;
  tmp_tm.tm_hour = hour;
  tmp_tm.tm_min = minute;
  tmp_tm.tm_sec = second;
  // BoringSSL POSIX time, like POSIX itself, does not support leap seconds.
  bool leap_second = false;
  if (tmp_tm.tm_sec == 60) {
    tmp_tm.tm_sec = 59;
    leap_second = true;
  }
  int64_t result;
  if (!OPENSSL_tm_to_posix(&tmp_tm, &result)) {
    return std::nullopt;
  }
  // Our desired behaviour is to return the following second for a leap second
  // assuming it is a valid time.
  if (leap_second) {
    if (!OPENSSL_posix_to_tm(result + 1, &tmp_tm)) {
      return std::nullopt;
    }
    result++;
  }
  return result;
}

}  // namespace quiche
