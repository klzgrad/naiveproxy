// Copyright (c) 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef QUICHE_COMMON_PLATFORM_API_QUICHE_MOCK_LOG_H_
#define QUICHE_COMMON_PLATFORM_API_QUICHE_MOCK_LOG_H_

#include "quiche_platform_impl/quiche_mock_log_impl.h"

using QuicheMockLog = QuicheMockLogImpl;
#define CREATE_QUICHE_MOCK_LOG(log) CREATE_QUICHE_MOCK_LOG_IMPL(log)

#define EXPECT_QUICHE_LOG_CALL(log) EXPECT_QUICHE_LOG_CALL_IMPL(log)

#define EXPECT_QUICHE_LOG_CALL_CONTAINS(log, level, content) \
  EXPECT_QUICHE_LOG_CALL_CONTAINS_IMPL(log, level, content)

#endif  // QUICHE_COMMON_PLATFORM_API_QUICHE_MOCK_LOG_H_
