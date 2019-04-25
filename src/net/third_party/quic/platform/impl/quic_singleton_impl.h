// Copyright (c) 2018 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef NET_THIRD_PARTY_QUIC_PLATFORM_IMPL_QUIC_SINGLETON_IMPL_H_
#define NET_THIRD_PARTY_QUIC_PLATFORM_IMPL_QUIC_SINGLETON_IMPL_H_

#include "base/memory/singleton.h"

namespace quic {

template <typename T>
using QuicSingletonImpl = base::Singleton<T, base::DefaultSingletonTraits<T>>;

template <typename T>
using QuicSingletonFriendImpl = base::DefaultSingletonTraits<T>;

}  // namespace quic

#endif  // NET_THIRD_PARTY_QUIC_PLATFORM_IMPL_QUIC_SINGLETON_IMPL_H_
