// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef QUICHE_HTTP2_PLATFORM_API_HTTP2_BUG_TRACKER_H_
#define QUICHE_HTTP2_PLATFORM_API_HTTP2_BUG_TRACKER_H_

#include "net/http2/platform/impl/http2_bug_tracker_impl.h"

#define HTTP2_BUG HTTP2_BUG_IMPL
#define HTTP2_BUG_IF HTTP2_BUG_IF_IMPL
#define FLAGS_http2_always_log_bugs_for_tests \
  FLAGS_http2_always_log_bugs_for_tests_IMPL

#endif  // QUICHE_HTTP2_PLATFORM_API_HTTP2_BUG_TRACKER_H_
