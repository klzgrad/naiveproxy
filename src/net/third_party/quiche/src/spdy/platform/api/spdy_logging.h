// Copyright 2019 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef QUICHE_SPDY_PLATFORM_API_SPDY_LOGGING_H_
#define QUICHE_SPDY_PLATFORM_API_SPDY_LOGGING_H_

#include "common/platform/api/quiche_logging.h"

#define SPDY_LOG(severity) QUICHE_LOG(severity)

#define SPDY_VLOG(verbose_level) QUICHE_VLOG(verbose_level)

#define SPDY_DLOG(severity) QUICHE_DLOG(severity)

#define SPDY_DLOG_IF(severity, condition) QUICHE_DLOG_IF(severity, condition)

#define SPDY_DVLOG(verbose_level) QUICHE_DVLOG(verbose_level)

#define SPDY_DVLOG_IF(verbose_level, condition) \
  QUICHE_DVLOG_IF(verbose_level, condition)

#endif  // QUICHE_SPDY_PLATFORM_API_SPDY_LOGGING_H_
