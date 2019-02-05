// Copyright 2018 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef NET_THIRD_PARTY_HTTP2_PLATFORM_IMPL_HTTP2_MOCK_LOG_IMPL_H_
#define NET_THIRD_PARTY_HTTP2_PLATFORM_IMPL_HTTP2_MOCK_LOG_IMPL_H_

#include "base/test/mock_log.h"
#include "testing/gmock/include/gmock/gmock.h"  // IWYU pragma: export

using Http2MockLogImpl = base::test::MockLog;
#define CREATE_HTTP2_MOCK_LOG_IMPL(log) Http2MockLog log

#define EXPECT_HTTP2_LOG_CALL_IMPL(log) \
  EXPECT_CALL(log,                      \
              Log(testing::_, testing::_, testing::_, testing::_, testing::_))

#define EXPECT_HTTP2_LOG_CALL_CONTAINS_IMPL(log, level, content)     \
  EXPECT_CALL(log, Log(logging::LOG_##level, testing::_, testing::_, \
                       testing::_, testing::HasSubstr(content)))

#endif  // NET_THIRD_PARTY_HTTP2_PLATFORM_IMPL_HTTP2_MOCK_LOG_IMPL_H_
