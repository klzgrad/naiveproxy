// Copyright (c) 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef QUICHE_COMMON_PLATFORM_API_QUICHE_STACK_TRACE_H_
#define QUICHE_COMMON_PLATFORM_API_QUICHE_STACK_TRACE_H_

#include <string>

#include "quiche_platform_impl/quiche_stack_trace_impl.h"

namespace quiche {

inline std::string QuicheStackTrace() { return QuicheStackTraceImpl(); }

}  // namespace quiche

#endif  // QUICHE_COMMON_PLATFORM_API_QUICHE_STACK_TRACE_H_
