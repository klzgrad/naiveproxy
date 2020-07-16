// Copyright 2018 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef NET_HTTP2_PLATFORM_IMPL_HTTP2_FLAG_UTILS_IMPL_H_
#define NET_HTTP2_PLATFORM_IMPL_HTTP2_FLAG_UTILS_IMPL_H_

#include "base/logging.h"

#define HTTP2_RELOADABLE_FLAG_COUNT_IMPL(flag) \
  DVLOG(1) << "FLAG_" #flag ": " << FLAGS_##flag

#endif  // NET_HTTP2_PLATFORM_IMPL_HTTP2_FLAG_UTILS_IMPL_H_
