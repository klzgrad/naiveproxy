// Copyright 2018 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef NET_THIRD_PARTY_HTTP2_PLATFORM_IMPL_HTTP2_FLAG_UTILS_IMPL_H_
#define NET_THIRD_PARTY_HTTP2_PLATFORM_IMPL_HTTP2_FLAG_UTILS_IMPL_H_

#include "base/logging.h"

#define HTTP2_FLAG_COUNT_IMPL(flag) \
  DVLOG(1) << "FLAG_" #flag ": " << FLAGS_##flag
#define HTTP2_FLAG_COUNT_N_IMPL(flag, instance, total) \
  HTTP2_FLAG_COUNT_IMPL(flag)

#define HTTP2_CODE_COUNT_IMPL(name) \
  do {                              \
  } while (0)
#define HTTP2_CODE_COUNT_N_IMPL(name, instance, total) \
  do {                                                 \
  } while (0)

#endif  // NET_THIRD_PARTY_HTTP2_PLATFORM_IMPL_HTTP2_FLAG_UTILS_IMPL_H_
