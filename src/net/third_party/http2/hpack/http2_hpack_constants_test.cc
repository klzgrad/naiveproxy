// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/third_party/http2/hpack/http2_hpack_constants.h"

#include "base/logging.h"
#include "testing/gmock/include/gmock/gmock.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace http2 {
namespace test {
namespace {

class HpackEntryTypeTest : public testing::Test {};

TEST(HpackEntryTypeTest, HpackEntryTypeToString) {
  EXPECT_EQ("kIndexedHeader",
            HpackEntryTypeToString(HpackEntryType::kIndexedHeader));
  EXPECT_EQ("kDynamicTableSizeUpdate",
            HpackEntryTypeToString(HpackEntryType::kDynamicTableSizeUpdate));
  EXPECT_EQ("kIndexedLiteralHeader",
            HpackEntryTypeToString(HpackEntryType::kIndexedLiteralHeader));
  EXPECT_EQ("kUnindexedLiteralHeader",
            HpackEntryTypeToString(HpackEntryType::kUnindexedLiteralHeader));
  EXPECT_EQ("kNeverIndexedLiteralHeader",
            HpackEntryTypeToString(HpackEntryType::kNeverIndexedLiteralHeader));
  EXPECT_EQ("UnknownHpackEntryType(12321)",
            HpackEntryTypeToString(static_cast<HpackEntryType>(12321)));
}

}  // namespace
}  // namespace test
}  // namespace http2
