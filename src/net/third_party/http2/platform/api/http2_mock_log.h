// Copyright 2018 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef NET_THIRD_PARTY_HTTP2_PLATFORM_API_HTTP2_MOCK_LOG_H_
#define NET_THIRD_PARTY_HTTP2_PLATFORM_API_HTTP2_MOCK_LOG_H_

#include "net/third_party/http2/platform/impl/http2_mock_log_impl.h"

using Http2MockLog = Http2MockLogImpl;
#define CREATE_HTTP2_MOCK_LOG(log) CREATE_HTTP2_MOCK_LOG_IMPL(log)

#define EXPECT_HTTP2_LOG_CALL(log) EXPECT_HTTP2_LOG_CALL_IMPL(log)

#define EXPECT_HTTP2_LOG_CALL_CONTAINS(log, level, content) \
  EXPECT_HTTP2_LOG_CALL_CONTAINS_IMPL(log, level, content)

#endif  // NET_THIRD_PARTY_HTTP2_PLATFORM_API_HTTP2_MOCK_LOG_H_
