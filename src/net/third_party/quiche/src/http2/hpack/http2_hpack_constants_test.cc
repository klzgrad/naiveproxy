// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/third_party/quiche/src/http2/hpack/http2_hpack_constants.h"

#include <sstream>

#include "testing/gmock/include/gmock/gmock.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "net/third_party/quiche/src/http2/platform/api/http2_logging.h"

namespace http2 {
namespace test {
namespace {

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

TEST(HpackEntryTypeTest, OutputHpackEntryType) {
  {
    std::stringstream log;
    log << HpackEntryType::kIndexedHeader;
    EXPECT_EQ("kIndexedHeader", log.str());
  }
  {
    std::stringstream log;
    log << HpackEntryType::kDynamicTableSizeUpdate;
    EXPECT_EQ("kDynamicTableSizeUpdate", log.str());
  }
  {
    std::stringstream log;
    log << HpackEntryType::kIndexedLiteralHeader;
    EXPECT_EQ("kIndexedLiteralHeader", log.str());
  }
  {
    std::stringstream log;
    log << HpackEntryType::kUnindexedLiteralHeader;
    EXPECT_EQ("kUnindexedLiteralHeader", log.str());
  }
  {
    std::stringstream log;
    log << HpackEntryType::kNeverIndexedLiteralHeader;
    EXPECT_EQ("kNeverIndexedLiteralHeader", log.str());
  }
  {
    std::stringstream log;
    log << static_cast<HpackEntryType>(1234321);
    EXPECT_EQ("UnknownHpackEntryType(1234321)", log.str());
  }
}

}  // namespace
}  // namespace test
}  // namespace http2
