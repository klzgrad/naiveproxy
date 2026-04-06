// Copyright (c) 2018 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef QUICHE_COMMON_PLATFORM_API_QUICHE_EXPECT_BUG_H_
#define QUICHE_COMMON_PLATFORM_API_QUICHE_EXPECT_BUG_H_

#include "quiche_platform_impl/quiche_expect_bug_impl.h"

#define EXPECT_QUICHE_BUG EXPECT_QUICHE_BUG_IMPL
#define EXPECT_QUICHE_PEER_BUG(statement, regex) \
  EXPECT_QUICHE_PEER_BUG_IMPL(statement, regex)

#endif  // QUICHE_COMMON_PLATFORM_API_QUICHE_EXPECT_BUG_H_
