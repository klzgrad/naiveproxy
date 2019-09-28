// Copyright (c) 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef QUICHE_QUIC_PLATFORM_API_QUIC_BUG_TRACKER_H_
#define QUICHE_QUIC_PLATFORM_API_QUIC_BUG_TRACKER_H_

#include "net/quic/platform/impl/quic_bug_tracker_impl.h"

#define QUIC_BUG QUIC_BUG_IMPL
#define QUIC_BUG_IF QUIC_BUG_IF_IMPL
#define QUIC_PEER_BUG QUIC_PEER_BUG_IMPL
#define QUIC_PEER_BUG_IF QUIC_PEER_BUG_IF_IMPL

#endif  // QUICHE_QUIC_PLATFORM_API_QUIC_BUG_TRACKER_H_
