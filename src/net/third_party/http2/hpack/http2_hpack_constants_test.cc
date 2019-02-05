// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/third_party/http2/hpack/http2_hpack_constants.h"

#include "base/logging.h"
#include "net/third_party/http2/platform/api/http2_mock_log.h"
#include "testing/gmock/include/gmock/gmock.h"
#include "testing/gtest/include/gtest/gtest.h"

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
    CREATE_HTTP2_MOCK_LOG(log);
    log.StartCapturingLogs();
    EXPECT_HTTP2_LOG_CALL_CONTAINS(log, INFO, "kIndexedHeader");
    LOG(INFO) << HpackEntryType::kIndexedHeader;
  }
  {
    CREATE_HTTP2_MOCK_LOG(log);
    log.StartCapturingLogs();
    EXPECT_HTTP2_LOG_CALL_CONTAINS(log, INFO, "kDynamicTableSizeUpdate");
    LOG(INFO) << HpackEntryType::kDynamicTableSizeUpdate;
  }
  {
    CREATE_HTTP2_MOCK_LOG(log);
    log.StartCapturingLogs();
    EXPECT_HTTP2_LOG_CALL_CONTAINS(log, INFO, "kIndexedLiteralHeader");
    LOG(INFO) << HpackEntryType::kIndexedLiteralHeader;
  }
  {
    CREATE_HTTP2_MOCK_LOG(log);
    log.StartCapturingLogs();
    EXPECT_HTTP2_LOG_CALL_CONTAINS(log, INFO, "kUnindexedLiteralHeader");
    LOG(INFO) << HpackEntryType::kUnindexedLiteralHeader;
  }
  {
    CREATE_HTTP2_MOCK_LOG(log);
    log.StartCapturingLogs();
    EXPECT_HTTP2_LOG_CALL_CONTAINS(log, INFO, "kNeverIndexedLiteralHeader");
    LOG(INFO) << HpackEntryType::kNeverIndexedLiteralHeader;
  }
  {
    CREATE_HTTP2_MOCK_LOG(log);
    log.StartCapturingLogs();
    EXPECT_HTTP2_LOG_CALL_CONTAINS(log, INFO, "UnknownHpackEntryType(1234321)");
    LOG(INFO) << static_cast<HpackEntryType>(1234321);
  }
}

}  // namespace
}  // namespace test
}  // namespace http2
