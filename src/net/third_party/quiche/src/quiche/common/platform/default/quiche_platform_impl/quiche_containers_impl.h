// Copyright 2021 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef QUICHE_COMMON_PLATFORM_DEFAULT_QUICHE_PLATFORM_IMPL_QUICHE_CONTAINERS_IMPL_H_
#define QUICHE_COMMON_PLATFORM_DEFAULT_QUICHE_PLATFORM_IMPL_QUICHE_CONTAINERS_IMPL_H_

#include "absl/container/btree_set.h"

namespace quiche {

template <typename Key, typename Compare>
using QuicheSmallOrderedSetImpl = absl::btree_set<Key, Compare>;

}  // namespace quiche

#endif  // QUICHE_COMMON_PLATFORM_DEFAULT_QUICHE_PLATFORM_IMPL_QUICHE_CONTAINERS_IMPL_H_
