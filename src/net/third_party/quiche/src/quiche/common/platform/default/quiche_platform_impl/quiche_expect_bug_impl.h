// Copyright 2022 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef QUICHE_COMMON_PLATFORM_DEFAULT_QUICHE_PLATFORM_IMPL_QUICHE_EXPECT_BUG_IMPL_H_
#define QUICHE_COMMON_PLATFORM_DEFAULT_QUICHE_PLATFORM_IMPL_QUICHE_EXPECT_BUG_IMPL_H_

#include "quiche/common/platform/api/quiche_test.h"

#define EXPECT_QUICHE_BUG_IMPL(statement, regex) \
  EXPECT_QUICHE_DEBUG_DEATH(statement, regex)
#define EXPECT_QUICHE_PEER_BUG_IMPL(statement, regex) \
  EXPECT_QUICHE_DEBUG_DEATH(statement, regex)

#endif  // QUICHE_COMMON_PLATFORM_DEFAULT_QUICHE_PLATFORM_IMPL_QUICHE_EXPECT_BUG_IMPL_H_
