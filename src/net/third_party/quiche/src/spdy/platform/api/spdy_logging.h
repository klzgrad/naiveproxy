// Copyright 2019 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef QUICHE_SPDY_PLATFORM_API_SPDY_LOGGING_H_
#define QUICHE_SPDY_PLATFORM_API_SPDY_LOGGING_H_

#include "net/spdy/platform/impl/spdy_logging_impl.h"

#define SPDY_LOG(severity) SPDY_LOG_IMPL(severity)

#define SPDY_VLOG(verbose_level) SPDY_VLOG_IMPL(verbose_level)

#define SPDY_DLOG(severity) SPDY_DLOG_IMPL(severity)

#define SPDY_DLOG_IF(severity, condition) SPDY_DLOG_IF_IMPL(severity, condition)

#define SPDY_DVLOG(verbose_level) SPDY_DVLOG_IMPL(verbose_level)

#define SPDY_DVLOG_IF(verbose_level, condition) \
  SPDY_DVLOG_IF_IMPL(verbose_level, condition)

#endif  // QUICHE_SPDY_PLATFORM_API_SPDY_LOGGING_H_
