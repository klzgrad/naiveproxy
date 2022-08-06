// Copyright (c) 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef QUICHE_COMMON_PLATFORM_API_QUICHE_STACK_TRACE_H_
#define QUICHE_COMMON_PLATFORM_API_QUICHE_STACK_TRACE_H_

#include <string>

#include "quiche_platform_impl/quiche_stack_trace_impl.h"

namespace quiche {

// Returns a human-readable stack trace.  Mostly used in error logging and
// related features.
inline std::string QuicheStackTrace() { return QuicheStackTraceImpl(); }

// Indicates whether the unit test for QuicheStackTrace() should be run.  The
// unit test calls QuicheStackTrace() from a specific function and checks
// whether that specific function is in the stack trace.  This function should
// return false if:
//   (1) QuicheStackTrace() is unimplemented,
//   (2) QuicheStackTrace() does not work on the current platform, or
//   (3) QuicheStackTrace() works, but the symbols are not guaranteed to be
//       available.
inline bool QuicheShouldRunStackTraceTest() {
  return QuicheShouldRunStackTraceTestImpl();
}

}  // namespace quiche

#endif  // QUICHE_COMMON_PLATFORM_API_QUICHE_STACK_TRACE_H_
