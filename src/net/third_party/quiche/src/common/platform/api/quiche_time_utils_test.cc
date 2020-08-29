// Copyright 2020 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/third_party/quiche/src/common/platform/api/quiche_time_utils.h"

#include "net/third_party/quiche/src/common/platform/api/quiche_optional.h"
#include "net/third_party/quiche/src/common/platform/api/quiche_test.h"

namespace quiche {
namespace {

TEST(QuicheTimeUtilsTest, Basic) {
  EXPECT_EQ(1, QuicheUtcDateTimeToUnixSeconds(1970, 1, 1, 0, 0, 1));
  EXPECT_EQ(365 * 86400, QuicheUtcDateTimeToUnixSeconds(1971, 1, 1, 0, 0, 0));
  // Some arbitrary timestamps closer to the present, compared to the output of
  // "Date(...).getTime()" from the JavaScript console.
  EXPECT_EQ(1152966896,
            QuicheUtcDateTimeToUnixSeconds(2006, 7, 15, 12, 34, 56));
  EXPECT_EQ(1591130001, QuicheUtcDateTimeToUnixSeconds(2020, 6, 2, 20, 33, 21));

  EXPECT_EQ(QUICHE_NULLOPT,
            QuicheUtcDateTimeToUnixSeconds(1970, 2, 29, 0, 0, 1));
  EXPECT_NE(QUICHE_NULLOPT,
            QuicheUtcDateTimeToUnixSeconds(1972, 2, 29, 0, 0, 1));
}

TEST(QuicheTimeUtilsTest, Bounds) {
  EXPECT_EQ(QUICHE_NULLOPT,
            QuicheUtcDateTimeToUnixSeconds(1970, 1, 32, 0, 0, 1));
  EXPECT_EQ(QUICHE_NULLOPT,
            QuicheUtcDateTimeToUnixSeconds(1970, 4, 31, 0, 0, 1));
  EXPECT_EQ(QUICHE_NULLOPT,
            QuicheUtcDateTimeToUnixSeconds(1970, 1, 0, 0, 0, 1));
  EXPECT_EQ(QUICHE_NULLOPT,
            QuicheUtcDateTimeToUnixSeconds(1970, 13, 1, 0, 0, 1));
  EXPECT_EQ(QUICHE_NULLOPT,
            QuicheUtcDateTimeToUnixSeconds(1970, 0, 1, 0, 0, 1));
  EXPECT_EQ(QUICHE_NULLOPT,
            QuicheUtcDateTimeToUnixSeconds(1970, 1, 1, 24, 0, 0));
  EXPECT_EQ(QUICHE_NULLOPT,
            QuicheUtcDateTimeToUnixSeconds(1970, 1, 1, 0, 60, 0));
}

TEST(QuicheTimeUtilsTest, LeapSecond) {
  EXPECT_EQ(QuicheUtcDateTimeToUnixSeconds(2015, 6, 30, 23, 59, 60),
            QuicheUtcDateTimeToUnixSeconds(2015, 7, 1, 0, 0, 0));
  EXPECT_EQ(QuicheUtcDateTimeToUnixSeconds(2015, 6, 30, 25, 59, 60),
            QUICHE_NULLOPT);
}

}  // namespace
}  // namespace quiche
