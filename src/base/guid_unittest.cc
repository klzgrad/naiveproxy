// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "base/guid.h"

#include <stdint.h>

#include <limits>

#include "base/strings/string_util.h"
#include "build/build_config.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace base {

namespace {

bool IsGUIDv4(const std::string& guid) {
  // The format of GUID version 4 must be xxxxxxxx-xxxx-4xxx-yxxx-xxxxxxxxxxxx,
  // where y is one of [8, 9, A, B].
  return IsValidGUID(guid) && guid[14] == '4' &&
         (guid[19] == '8' || guid[19] == '9' || guid[19] == 'A' ||
          guid[19] == 'a' || guid[19] == 'B' || guid[19] == 'b');
}

}  // namespace

TEST(GUIDTest, GUIDGeneratesAllZeroes) {
  uint64_t bytes[] = {0, 0};
  std::string clientid = RandomDataToGUIDString(bytes);
  EXPECT_EQ("00000000-0000-0000-0000-000000000000", clientid);
}

TEST(GUIDTest, GUIDGeneratesCorrectly) {
  uint64_t bytes[] = {0x0123456789ABCDEFULL, 0xFEDCBA9876543210ULL};
  std::string clientid = RandomDataToGUIDString(bytes);
  EXPECT_EQ("01234567-89ab-cdef-fedc-ba9876543210", clientid);
}

TEST(GUIDTest, GUIDCorrectlyFormatted) {
  const int kIterations = 10;
  for (int it = 0; it < kIterations; ++it) {
    std::string guid = GenerateGUID();
    EXPECT_TRUE(IsValidGUID(guid));
    EXPECT_TRUE(IsValidGUIDOutputString(guid));
    EXPECT_TRUE(IsValidGUID(ToLowerASCII(guid)));
    EXPECT_TRUE(IsValidGUID(ToUpperASCII(guid)));
  }
}

TEST(GUIDTest, GUIDBasicUniqueness) {
  const int kIterations = 10;
  for (int it = 0; it < kIterations; ++it) {
    std::string guid1 = GenerateGUID();
    std::string guid2 = GenerateGUID();
    EXPECT_EQ(36U, guid1.length());
    EXPECT_EQ(36U, guid2.length());
    EXPECT_NE(guid1, guid2);
    EXPECT_TRUE(IsGUIDv4(guid1));
    EXPECT_TRUE(IsGUIDv4(guid2));
  }
}

}  // namespace base
