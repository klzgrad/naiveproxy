// Copyright (c) 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef QUICHE_COMMON_PLATFORM_API_QUICHE_LOGGING_H_
#define QUICHE_COMMON_PLATFORM_API_QUICHE_LOGGING_H_

#include "net/quiche/common/platform/impl/quiche_logging_impl.h"

// Please note following QUICHE_LOG are platform dependent:
// INFO severity can be degraded (to VLOG(1) or DVLOG(1)).
// Some platforms may not support QUICHE_LOG_FIRST_N or QUICHE_LOG_EVERY_N_SEC,
// and they would simply be translated to LOG.

#define QUICHE_DVLOG(verbose_level) QUICHE_DVLOG_IMPL(verbose_level)
#define QUICHE_DVLOG_IF(verbose_level, condition) \
  QUICHE_DVLOG_IF_IMPL(verbose_level, condition)
#define QUICHE_DLOG(severity) QUICHE_DLOG_IMPL(severity)
#define QUICHE_DLOG_IF(severity, condition) \
  QUICHE_DLOG_IF_IMPL(severity, condition)
#define QUICHE_VLOG(verbose_level) QUICHE_VLOG_IMPL(verbose_level)
#define QUICHE_LOG(severity) QUICHE_LOG_IMPL(severity)
#define QUICHE_LOG_FIRST_N(severity, n) QUICHE_LOG_FIRST_N_IMPL(severity, n)
#define QUICHE_LOG_EVERY_N_SEC(severity, seconds) \
  QUICHE_LOG_EVERY_N_SEC_IMPL(severity, seconds)
#define QUICHE_LOG_IF(severity, condition) \
  QUICHE_LOG_IF_IMPL(severity, condition)

#define QUICHE_PREDICT_FALSE(x) QUICHE_PREDICT_FALSE_IMPL(x)
#define QUICHE_PREDICT_TRUE(x) QUICHE_PREDICT_TRUE_IMPL(x)

// This is a noop in release build.
#define QUICHE_NOTREACHED() QUICHE_NOTREACHED_IMPL()

#define QUICHE_PLOG(severity) QUICHE_PLOG_IMPL(severity)

#define QUICHE_DLOG_INFO_IS_ON() QUICHE_DLOG_INFO_IS_ON_IMPL()
#define QUICHE_LOG_INFO_IS_ON() QUICHE_LOG_INFO_IS_ON_IMPL()
#define QUICHE_LOG_WARNING_IS_ON() QUICHE_LOG_WARNING_IS_ON_IMPL()
#define QUICHE_LOG_ERROR_IS_ON() QUICHE_LOG_ERROR_IS_ON_IMPL()

#endif  // QUICHE_COMMON_PLATFORM_API_QUICHE_LOGGING_H_
