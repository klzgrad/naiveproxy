// Copyright 2018 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef NET_THIRD_PARTY_QUIC_PLATFORM_IMPL_QUIC_UINT128_IMPL_H_
#define NET_THIRD_PARTY_QUIC_PLATFORM_IMPL_QUIC_UINT128_IMPL_H_

#include "net/base/int128.h"

namespace quic {

using QuicUint128Impl = net::uint128;
#define MakeQuicUint128Impl(hi, lo) net::MakeUint128(hi, lo)
#define QuicUint128Low64Impl(x) Uint128Low64(x)
#define QuicUint128High64Impl(x) Uint128High64(x)

}  // namespace quic

#endif  // NET_THIRD_PARTY_QUIC_PLATFORM_IMPL_QUIC_UINT128_IMPL_H_
