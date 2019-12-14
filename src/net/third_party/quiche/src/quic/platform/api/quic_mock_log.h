// Copyright (c) 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef QUICHE_QUIC_PLATFORM_API_QUIC_MOCK_LOG_H_
#define QUICHE_QUIC_PLATFORM_API_QUIC_MOCK_LOG_H_

#include "net/quic/platform/impl/quic_mock_log_impl.h"

using QuicMockLog = QuicMockLogImpl;
#define CREATE_QUIC_MOCK_LOG(log) CREATE_QUIC_MOCK_LOG_IMPL(log)

#define EXPECT_QUIC_LOG_CALL(log) EXPECT_QUIC_LOG_CALL_IMPL(log)

#define EXPECT_QUIC_LOG_CALL_CONTAINS(log, level, content) \
  EXPECT_QUIC_LOG_CALL_CONTAINS_IMPL(log, level, content)

#endif  // QUICHE_QUIC_PLATFORM_API_QUIC_MOCK_LOG_H_
