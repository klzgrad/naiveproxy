// Copyright (c) 2018 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef QUICHE_QUIC_PLATFORM_API_QUIC_EXPECT_BUG_H_
#define QUICHE_QUIC_PLATFORM_API_QUIC_EXPECT_BUG_H_

#include "quiche/common/platform/api/quiche_expect_bug.h"

#define EXPECT_QUIC_BUG EXPECT_QUICHE_BUG
#define EXPECT_QUIC_PEER_BUG(statement, regex) \
  EXPECT_QUICHE_PEER_BUG(statement, regex)

#endif  // QUICHE_QUIC_PLATFORM_API_QUIC_EXPECT_BUG_H_
