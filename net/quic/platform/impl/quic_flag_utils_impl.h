// Copyright (c) 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef NET_QUIC_PLATFORM_IMPL_QUIC_FLAG_UTILS_IMPL_H_
#define NET_QUIC_PLATFORM_IMPL_QUIC_FLAG_UTILS_IMPL_H_

#define QUIC_FLAG_COUNT_IMPL(flag) \
  DVLOG(1) << "FLAG_" #flag ": " << FLAGS_##flag
#define QUIC_FLAG_COUNT_N_IMPL(flag, instance, total) QUIC_FLAG_COUNT_IMPL(flag)

#define QUIC_CODE_COUNT_IMPL(name) \
  do {                             \
  } while (0)
#define QUIC_CODE_COUNT_N_IMPL(name, instance, total) \
  do {                                                \
  } while (0)

#endif  // NET_QUIC_PLATFORM_IMPL_QUIC_FLAG_UTILS_IMPL_H_
