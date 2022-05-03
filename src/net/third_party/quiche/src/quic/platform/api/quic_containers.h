// Copyright (c) 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef QUICHE_QUIC_PLATFORM_API_QUIC_CONTAINERS_H_
#define QUICHE_QUIC_PLATFORM_API_QUIC_CONTAINERS_H_

#include "quiche_platform_impl/quiche_containers_impl.h"

namespace quic {

// An ordered container optimized for small sets.
// An implementation with O(n) mutations might be chosen
// in case it has better memory usage and/or faster access.
//
// DOES NOT GUARANTEE POINTER OR ITERATOR STABILITY!
template <typename Key, typename Compare = std::less<Key>>
using QuicSmallOrderedSet = ::quiche::QuicheSmallOrderedSetImpl<Key, Compare>;

}  // namespace quic

#endif  // QUICHE_QUIC_PLATFORM_API_QUIC_CONTAINERS_H_
