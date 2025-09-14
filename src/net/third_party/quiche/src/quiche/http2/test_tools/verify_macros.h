// Copyright 2022 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef QUICHE_HTTP2_TEST_TOOLS_VERIFY_MACROS_H_
#define QUICHE_HTTP2_TEST_TOOLS_VERIFY_MACROS_H_

#include "quiche/common/platform/api/quiche_test.h"

#define HTTP2_VERIFY_CORE(value, str)            \
  if ((value))                                   \
    ;                                            \
  else                                           \
    return ::testing::AssertionFailure()         \
           << __FILE__ << ":" << __LINE__ << " " \
           << "Failed to verify that '" << str << "'"

#define HTTP2_VERIFY_TRUE(value) HTTP2_VERIFY_CORE(value, #value)
#define HTTP2_VERIFY_FALSE(value) HTTP2_VERIFY_CORE(!value, "!" #value)
#define HTTP2_VERIFY_SUCCESS HTTP2_VERIFY_TRUE
#define HTTP2_VERIFY_EQ(value1, value2) \
  HTTP2_VERIFY_CORE((value1) == (value2), #value1 "==" #value2)
#define HTTP2_VERIFY_NE(value1, value2) \
  HTTP2_VERIFY_CORE((value1) != (value2), #value1 "!=" #value2)
#define HTTP2_VERIFY_LE(value1, value2) \
  HTTP2_VERIFY_CORE((value1) <= (value2), #value1 "<=" #value2)
#define HTTP2_VERIFY_LT(value1, value2) \
  HTTP2_VERIFY_CORE((value1) < (value2), #value1 "<" #value2)
#define HTTP2_VERIFY_GT(value1, value2) \
  HTTP2_VERIFY_CORE((value1) > (value2), #value1 ">" #value2)

#endif  // QUICHE_HTTP2_TEST_TOOLS_VERIFY_MACROS_H_
