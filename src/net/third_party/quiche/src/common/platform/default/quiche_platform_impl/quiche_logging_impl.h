// Copyright 2021 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// This file does not actually implement logging, it merely provides enough of
// logging code for QUICHE to compile.  QUICHE embedders are encouraged to
// override this file with their own logic.  If at some point logging becomes a
// part of Abseil, this file will likely start using that instead.

#ifndef QUICHE_COMMON_PLATFORM_DEFAULT_QUICHE_PLATFORM_IMPL_QUICHE_LOGGING_IMPL_H_
#define QUICHE_COMMON_PLATFORM_DEFAULT_QUICHE_PLATFORM_IMPL_QUICHE_LOGGING_IMPL_H_

#include <cstdlib>
#include <iostream>
#include <sstream>
#include <string>

#include "absl/base/attributes.h"

namespace quiche {

// NoopLogSink provides a log sink that does not put the data that it logs
// anywhere.
class NoopLogSink {
 public:
  NoopLogSink() {}

  template <typename T>
  constexpr NoopLogSink(const T&) {}

  template <typename T1, typename T2>
  constexpr NoopLogSink(const T1&, const T2&) {}

  constexpr std::ostream& stream() { return stream_; }

  // This operator has lower precedence than << but higher than ?:, which is
  // useful for implementing QUICHE_DISREGARD_LOG_STREAM below.
  void operator&(std::ostream&) {}

 private:
  std::stringstream stream_;
};

// We need to actually implement LOG(FATAL), otherwise some functions will fail
// to compile due to the "failed to return value from non-void function" error.
class FatalLogSink : public NoopLogSink {
 public:
  ABSL_ATTRIBUTE_NORETURN ~FatalLogSink() { abort(); }
};

}  // namespace quiche

#define QUICHE_DVLOG_IMPL(verbose_level) \
  ::quiche::NoopLogSink(#verbose_level).stream()
#define QUICHE_DVLOG_IF_IMPL(verbose_level, condition) \
  ::quiche::NoopLogSink(#verbose_level, condition).stream()
#define QUICHE_DLOG_IMPL(severity) ::quiche::NoopLogSink(#severity).stream()
#define QUICHE_DLOG_IF_IMPL(severity, condition) \
  ::quiche::NoopLogSink(#severity, condition).stream()
#define QUICHE_VLOG_IMPL(verbose_level) \
  ::quiche::NoopLogSink(#verbose_level).stream()
#define QUICHE_LOG_FIRST_N_IMPL(severity, n) \
  ::quiche::NoopLogSink(#severity, n).stream()
#define QUICHE_LOG_EVERY_N_SEC_IMPL(severity, seconds) \
  ::quiche::NoopLogSink(#severity, seconds).stream()
#define QUICHE_LOG_IF_IMPL(severity, condition) \
  ::quiche::NoopLogSink(#severity, condition).stream()

#define QUICHE_LOG_IMPL(severity) QUICHE_LOG_IMPL_##severity()
#define QUICHE_LOG_IMPL_FATAL() ::quiche::FatalLogSink().stream()
#define QUICHE_LOG_IMPL_DFATAL() ::quiche::NoopLogSink().stream()
#define QUICHE_LOG_IMPL_ERROR() ::quiche::NoopLogSink().stream()
#define QUICHE_LOG_IMPL_WARNING() ::quiche::NoopLogSink().stream()
#define QUICHE_LOG_IMPL_INFO() ::quiche::NoopLogSink().stream()

#define QUICHE_PREDICT_FALSE_IMPL(x) (x)
#define QUICHE_PREDICT_TRUE_IMPL(x) (x)

#define QUICHE_PLOG_IMPL(severity) ::quiche::NoopLogSink(#severity)

#define QUICHE_DLOG_INFO_IS_ON_IMPL() false
#define QUICHE_LOG_INFO_IS_ON_IMPL() false
#define QUICHE_LOG_WARNING_IS_ON_IMPL() false
#define QUICHE_LOG_ERROR_IS_ON_IMPL() false

// This is necessary because we sometimes call QUICHE_DCHECK inside constexpr
// functions, and then write non-constexpr expressions into the resulting log.
#define QUICHE_DISREGARD_LOG_STREAM(stream) \
  true ? (void)0 : ::quiche::NoopLogSink() & (stream)

#define QUICHE_CHECK_IMPL(condition) ::quiche::NoopLogSink(condition).stream()
#define QUICHE_CHECK_EQ_IMPL(val1, val2) \
  ::quiche::NoopLogSink(val1, val2).stream()
#define QUICHE_CHECK_NE_IMPL(val1, val2) \
  ::quiche::NoopLogSink(val1, val2).stream()
#define QUICHE_CHECK_LE_IMPL(val1, val2) \
  ::quiche::NoopLogSink(val1, val2).stream()
#define QUICHE_CHECK_LT_IMPL(val1, val2) \
  ::quiche::NoopLogSink(val1, val2).stream()
#define QUICHE_CHECK_GE_IMPL(val1, val2) \
  ::quiche::NoopLogSink(val1, val2).stream()
#define QUICHE_CHECK_GT_IMPL(val1, val2) \
  ::quiche::NoopLogSink(val1, val2).stream()

#define QUICHE_DCHECK_IMPL(condition) \
  QUICHE_DISREGARD_LOG_STREAM(::quiche::NoopLogSink(condition).stream())
#define QUICHE_DCHECK_EQ_IMPL(val1, val2) \
  ::quiche::NoopLogSink(val1, val2).stream()
#define QUICHE_DCHECK_NE_IMPL(val1, val2) \
  ::quiche::NoopLogSink(val1, val2).stream()
#define QUICHE_DCHECK_LE_IMPL(val1, val2) \
  ::quiche::NoopLogSink(val1, val2).stream()
#define QUICHE_DCHECK_LT_IMPL(val1, val2) \
  ::quiche::NoopLogSink(val1, val2).stream()
#define QUICHE_DCHECK_GE_IMPL(val1, val2) \
  ::quiche::NoopLogSink(val1, val2).stream()
#define QUICHE_DCHECK_GT_IMPL(val1, val2) \
  ::quiche::NoopLogSink(val1, val2).stream()

#define QUICHE_NOTREACHED_IMPL() QUICHE_DCHECK_IMPL(false)

#endif  // QUICHE_COMMON_PLATFORM_DEFAULT_QUICHE_PLATFORM_IMPL_QUICHE_LOGGING_IMPL_H_
