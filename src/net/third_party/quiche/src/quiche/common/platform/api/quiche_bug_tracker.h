// Copyright (c) 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef QUICHE_COMMON_PLATFORM_API_QUICHE_BUG_TRACKER_H_
#define QUICHE_COMMON_PLATFORM_API_QUICHE_BUG_TRACKER_H_

#include "quiche_platform_impl/quiche_bug_tracker_impl.h"

#define QUICHE_BUG QUICHE_BUG_IMPL
#define QUICHE_BUG_IF QUICHE_BUG_IF_IMPL
#define QUICHE_PEER_BUG QUICHE_PEER_BUG_IMPL
#define QUICHE_PEER_BUG_IF QUICHE_PEER_BUG_IF_IMPL

#endif  // QUICHE_COMMON_PLATFORM_API_QUICHE_BUG_TRACKER_H_
