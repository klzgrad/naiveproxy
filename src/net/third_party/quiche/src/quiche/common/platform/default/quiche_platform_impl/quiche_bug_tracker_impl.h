// Copyright 2021 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef QUICHE_COMMON_PLATFORM_DEFAULT_QUICHE_PLATFORM_IMPL_QUICHE_BUG_TRACKER_IMPL_H_
#define QUICHE_COMMON_PLATFORM_DEFAULT_QUICHE_PLATFORM_IMPL_QUICHE_BUG_TRACKER_IMPL_H_

#include "quiche/common/platform/api/quiche_logging.h"

#define QUICHE_BUG_IMPL(b) QUICHE_LOG(DFATAL) << #b ": "
#define QUICHE_BUG_IF_IMPL(b, condition) \
  QUICHE_LOG_IF(DFATAL, condition) << #b ": "
#define QUICHE_PEER_BUG_IMPL(b) QUICHE_LOG(DFATAL) << #b ": "
#define QUICHE_PEER_BUG_IF_IMPL(b, condition) \
  QUICHE_LOG_IF(DFATAL, condition) << #b ": "

#endif  // QUICHE_COMMON_PLATFORM_DEFAULT_QUICHE_PLATFORM_IMPL_QUICHE_BUG_TRACKER_IMPL_H_
