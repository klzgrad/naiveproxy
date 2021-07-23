// Copyright (c) 2018 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef QUICHE_SPDY_PLATFORM_API_SPDY_CONTAINERS_H_
#define QUICHE_SPDY_PLATFORM_API_SPDY_CONTAINERS_H_

#include "net/spdy/platform/impl/spdy_containers_impl.h"

namespace spdy {

// A map which offers insertion-ordered iteration.
template <typename Key, typename Value, typename Hash, typename Eq>
using SpdyLinkedHashMap = SpdyLinkedHashMapImpl<Key, Value, Hash, Eq>;

}  // namespace spdy

#endif  // QUICHE_SPDY_PLATFORM_API_SPDY_CONTAINERS_H_
