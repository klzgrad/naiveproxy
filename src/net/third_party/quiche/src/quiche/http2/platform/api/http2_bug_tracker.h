// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef QUICHE_HTTP2_PLATFORM_API_HTTP2_BUG_TRACKER_H_
#define QUICHE_HTTP2_PLATFORM_API_HTTP2_BUG_TRACKER_H_

#include "quiche/common/platform/api/quiche_bug_tracker.h"

#define HTTP2_BUG QUICHE_BUG
#define HTTP2_BUG_IF QUICHE_BUG_IF

// V2 macros are the same as all the HTTP2_BUG flavor above, but they take a
// bug_id parameter.
#define HTTP2_BUG_V2 QUICHE_BUG
#define HTTP2_BUG_IF_V2 QUICHE_BUG_IF

#define FLAGS_http2_always_log_bugs_for_tests \
  FLAGS_http2_always_log_bugs_for_tests_IMPL

#endif  // QUICHE_HTTP2_PLATFORM_API_HTTP2_BUG_TRACKER_H_
