// Copyright 2022 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef QUICHE_COMMON_PLATFORM_DEFAULT_QUICHE_PLATFORM_IMPL_QUICHE_STACK_TRACE_IMPL_H_
#define QUICHE_COMMON_PLATFORM_DEFAULT_QUICHE_PLATFORM_IMPL_QUICHE_STACK_TRACE_IMPL_H_

#include <string>
#include <vector>

#include "absl/types/span.h"

namespace quiche {

std::vector<void*> CurrentStackTraceImpl();
std::string SymbolizeStackTraceImpl(absl::Span<void* const> stacktrace);
std::string QuicheStackTraceImpl();
bool QuicheShouldRunStackTraceTestImpl();

}  // namespace quiche

#endif  // QUICHE_COMMON_PLATFORM_DEFAULT_QUICHE_PLATFORM_IMPL_QUICHE_STACK_TRACE_IMPL_H_
