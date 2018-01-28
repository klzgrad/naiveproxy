// Copyright (c) 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef NET_SPDY_CORE_SPDY_BUG_TRACKER_H_
#define NET_SPDY_CORE_SPDY_BUG_TRACKER_H_

#include "base/logging.h"

#define SPDY_BUG LOG(DFATAL)
#define SPDY_BUG_IF(condition) LOG_IF(DFATAL, (condition))
#define FLAGS_spdy_always_log_bugs_for_tests (true)

#endif  // NET_SPDY_CORE_SPDY_BUG_TRACKER_H_
