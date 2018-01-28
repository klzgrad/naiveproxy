// Copyright (c) 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef NET_QUIC_PLATFORM_API_QUIC_FLAGS_H_
#define NET_QUIC_PLATFORM_API_QUIC_FLAGS_H_

#include "net/quic/platform/impl/quic_flags_impl.h"

#define GetQuicFlag(flag) GetQuicFlagImpl(flag)
#define SetQuicFlag(flag, value) SetQuicFlagImpl(flag, value)

#endif  // NET_QUIC_PLATFORM_API_QUIC_FLAGS_H_
