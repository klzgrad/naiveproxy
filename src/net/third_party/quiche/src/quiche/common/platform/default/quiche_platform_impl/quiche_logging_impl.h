// Copyright 2021 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// This file implements logging using Abseil.

#ifndef QUICHE_COMMON_PLATFORM_DEFAULT_QUICHE_PLATFORM_IMPL_QUICHE_LOGGING_IMPL_H_
#define QUICHE_COMMON_PLATFORM_DEFAULT_QUICHE_PLATFORM_IMPL_QUICHE_LOGGING_IMPL_H_

#include "absl/base/log_severity.h"
#include "absl/flags/declare.h"
#include "absl/flags/flag.h"
#include "absl/log/absl_check.h"
#include "absl/log/absl_log.h"

ABSL_DECLARE_FLAG(int, v);

#define QUICHE_LOG_IMPL(severity) QUICHE_LOG_IMPL_##severity()
#define QUICHE_LOG_IMPL_FATAL() ABSL_LOG(FATAL)
#define QUICHE_LOG_IMPL_ERROR() ABSL_LOG(ERROR)
#define QUICHE_LOG_IMPL_WARNING() ABSL_LOG(WARNING)
#define QUICHE_LOG_IMPL_INFO() ABSL_LOG(INFO)

#define QUICHE_PLOG_IMPL(severity) QUICHE_PLOG_IMPL_##severity()
#define QUICHE_PLOG_IMPL_FATAL() ABSL_PLOG(FATAL)
#define QUICHE_PLOG_IMPL_ERROR() ABSL_PLOG(ERROR)
#define QUICHE_PLOG_IMPL_WARNING() ABSL_PLOG(WARNING)
#define QUICHE_PLOG_IMPL_INFO() ABSL_PLOG(INFO)

#define QUICHE_DLOG_IMPL(severity) QUICHE_DLOG_IMPL_##severity()
#define QUICHE_DLOG_IMPL_FATAL() ABSL_DLOG(FATAL)
#define QUICHE_DLOG_IMPL_ERROR() ABSL_DLOG(ERROR)
#define QUICHE_DLOG_IMPL_WARNING() ABSL_DLOG(WARNING)
#define QUICHE_DLOG_IMPL_INFO() ABSL_DLOG(INFO)

#define QUICHE_LOG_IF_IMPL(severity, condition) \
  QUICHE_LOG_IF_IMPL_##severity(condition)
#define QUICHE_LOG_IF_IMPL_FATAL(condition) ABSL_LOG_IF(FATAL, condition)
#define QUICHE_LOG_IF_IMPL_ERROR(condition) ABSL_LOG_IF(ERROR, condition)
#define QUICHE_LOG_IF_IMPL_WARNING(condition) ABSL_LOG_IF(WARNING, condition)
#define QUICHE_LOG_IF_IMPL_INFO(condition) ABSL_LOG_IF(INFO, condition)

#define QUICHE_PLOG_IF_IMPL(severity, condition) \
  QUICHE_PLOG_IF_IMPL_##severity(condition)
#define QUICHE_PLOG_IF_IMPL_FATAL(condition) ABSL_PLOG_IF(FATAL, condition)
#define QUICHE_PLOG_IF_IMPL_ERROR(condition) ABSL_PLOG_IF(ERROR, condition)
#define QUICHE_PLOG_IF_IMPL_WARNING(condition) ABSL_PLOG_IF(WARNING, condition)
#define QUICHE_PLOG_IF_IMPL_INFO(condition) ABSL_PLOG_IF(INFO, condition)

#define QUICHE_DLOG_IF_IMPL(severity, condition) \
  QUICHE_DLOG_IF_IMPL_##severity(condition)
#define QUICHE_DLOG_IF_IMPL_FATAL(condition) ABSL_DLOG_IF(FATAL, condition)
#define QUICHE_DLOG_IF_IMPL_ERROR(condition) ABSL_DLOG_IF(ERROR, condition)
#define QUICHE_DLOG_IF_IMPL_WARNING(condition) ABSL_DLOG_IF(WARNING, condition)
#define QUICHE_DLOG_IF_IMPL_INFO(condition) ABSL_DLOG_IF(INFO, condition)

#define QUICHE_LOG_FIRST_N_IMPL(severity, n) \
  QUICHE_LOG_FIRST_N_IMPL_##severity(n)
#define QUICHE_LOG_FIRST_N_IMPL_FATAL(n) ABSL_LOG_FIRST_N(FATAL, n)
#define QUICHE_LOG_FIRST_N_IMPL_ERROR(n) ABSL_LOG_FIRST_N(ERROR, n)
#define QUICHE_LOG_FIRST_N_IMPL_WARNING(n) ABSL_LOG_FIRST_N(WARNING, n)
#define QUICHE_LOG_FIRST_N_IMPL_INFO(n) ABSL_LOG_FIRST_N(INFO, n)

#define QUICHE_LOG_EVERY_N_SEC_IMPL(severity, seconds) \
  QUICHE_LOG_EVERY_N_SEC_IMPL_##severity(seconds)
#define QUICHE_LOG_EVERY_N_SEC_IMPL_FATAL(seconds) \
  ABSL_LOG_EVERY_N_SEC(FATAL, seconds)
#define QUICHE_LOG_EVERY_N_SEC_IMPL_ERROR(seconds) \
  ABSL_LOG_EVERY_N_SEC(ERROR, seconds)
#define QUICHE_LOG_EVERY_N_SEC_IMPL_WARNING(seconds) \
  ABSL_LOG_EVERY_N_SEC(WARNING, seconds)
#define QUICHE_LOG_EVERY_N_SEC_IMPL_INFO(seconds) \
  ABSL_LOG_EVERY_N_SEC(INFO, seconds)

// Implement DFATAL pseudo-severity locally until Abseil exports one.
#ifdef NDEBUG
#define QUICHE_LOG_IMPL_DFATAL() ABSL_LOG(ERROR)
#define QUICHE_PLOG_IMPL_DFATAL() ABSL_PLOG(ERROR)
#define QUICHE_DLOG_IMPL_DFATAL() ABSL_DLOG(ERROR)
#define QUICHE_LOG_IF_IMPL_DFATAL(condition) ABSL_LOG_IF(ERROR, condition)
#define QUICHE_PLOG_IF_IMPL_DFATAL(condition) ABSL_PLOG_IF(ERROR, condition)
#define QUICHE_DLOG_IF_IMPL_DFATAL(condition) ABSL_DLOG_IF(ERROR, condition)
#define QUICHE_LOG_FIRST_N_IMPL_DFATAL(n) ABSL_LOG_FIRST_N(ERROR, n)
#define QUICHE_LOG_EVERY_N_SEC_IMPL_DFATAL(seconds) \
  ABSL_LOG_EVERY_N_SEC(ERROR, seconds)
#else
#define QUICHE_LOG_IMPL_DFATAL() ABSL_LOG(FATAL)
#define QUICHE_PLOG_IMPL_DFATAL() ABSL_PLOG(FATAL)
#define QUICHE_DLOG_IMPL_DFATAL() ABSL_DLOG(FATAL)
#define QUICHE_LOG_IF_IMPL_DFATAL(condition) ABSL_LOG_IF(FATAL, condition)
#define QUICHE_PLOG_IF_IMPL_DFATAL(condition) ABSL_PLOG_IF(FATAL, condition)
#define QUICHE_DLOG_IF_IMPL_DFATAL(condition) ABSL_DLOG_IF(FATAL, condition)
#define QUICHE_LOG_FIRST_N_IMPL_DFATAL(n) ABSL_LOG_FIRST_N(FATAL, n)
#define QUICHE_LOG_EVERY_N_SEC_IMPL_DFATAL(seconds) \
  ABSL_LOG_EVERY_N_SEC(FATAL, seconds)
#endif  // NDEBUG

// Implement VLOG and DVLOG in terms of LOG and DLOG.
#define QUICHE_VLOG_PREDICATE(verbose_level) \
  (verbose_level <= absl::GetFlag(FLAGS_v))

#define QUICHE_VLOG_IMPL(verbose_level) \
  QUICHE_LOG_IF_IMPL(INFO, QUICHE_VLOG_PREDICATE(verbose_level))
#define QUICHE_VLOG_IF_IMPL(verbose_level, condition) \
  QUICHE_LOG_IF_IMPL(INFO, (QUICHE_VLOG_PREDICATE(verbose_level) && condition))
#define QUICHE_DVLOG_IMPL(verbose_level) \
  QUICHE_DLOG_IF_IMPL(INFO, QUICHE_VLOG_PREDICATE(verbose_level))
#define QUICHE_DVLOG_IF_IMPL(verbose_level, condition) \
  QUICHE_DLOG_IF_IMPL(INFO, (QUICHE_VLOG_PREDICATE(verbose_level) && condition))

#define QUICHE_LOG_INFO_IS_ON_IMPL() 1
#define QUICHE_LOG_WARNING_IS_ON_IMPL() 1
#define QUICHE_LOG_ERROR_IS_ON_IMPL() 1

#ifdef NDEBUG
#define QUICHE_DLOG_INFO_IS_ON_IMPL() 0
#else
#define QUICHE_DLOG_INFO_IS_ON_IMPL() 1
#endif  // NDEBUG

#define QUICHE_CHECK_IMPL ABSL_CHECK
#define QUICHE_CHECK_EQ_IMPL ABSL_CHECK_EQ
#define QUICHE_CHECK_NE_IMPL ABSL_CHECK_NE
#define QUICHE_CHECK_LE_IMPL ABSL_CHECK_LE
#define QUICHE_CHECK_LT_IMPL ABSL_CHECK_LT
#define QUICHE_CHECK_GE_IMPL ABSL_CHECK_GE
#define QUICHE_CHECK_GT_IMPL ABSL_CHECK_GT
#define QUICHE_CHECK_OK_IMPL ABSL_CHECK_OK

#define QUICHE_DCHECK_IMPL ABSL_DCHECK
#define QUICHE_DCHECK_EQ_IMPL ABSL_DCHECK_EQ
#define QUICHE_DCHECK_NE_IMPL ABSL_DCHECK_NE
#define QUICHE_DCHECK_LE_IMPL ABSL_DCHECK_LE
#define QUICHE_DCHECK_LT_IMPL ABSL_DCHECK_LT
#define QUICHE_DCHECK_GE_IMPL ABSL_DCHECK_GE
#define QUICHE_DCHECK_GT_IMPL ABSL_DCHECK_GT

#define QUICHE_NOTREACHED_IMPL() QUICHE_DCHECK_IMPL(false)

#endif  // QUICHE_COMMON_PLATFORM_DEFAULT_QUICHE_PLATFORM_IMPL_QUICHE_LOGGING_IMPL_H_
