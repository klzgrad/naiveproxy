// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef NET_THIRD_PARTY_QUIC_PLATFORM_API_QUIC_OPTIONAL_H_
#define NET_THIRD_PARTY_QUIC_PLATFORM_API_QUIC_OPTIONAL_H_

#include "net/third_party/quic/platform/impl/quic_optional_impl.h"

namespace net {

template <typename T>
using QuicOptional = QuicOptionalImpl<T>;

}  // namespace net

#endif  // NET_THIRD_PARTY_QUIC_PLATFORM_API_QUIC_OPTIONAL_H_
