// Copyright 2022 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "quiche_platform_impl/quiche_stack_trace_impl.h"

#include <string>
#include <vector>

#include "absl/base/macros.h"
#include "absl/debugging/stacktrace.h"
#include "absl/debugging/symbolize.h"
#include "absl/strings/str_format.h"
#include "absl/strings/string_view.h"
#include "absl/types/span.h"

namespace quiche {

namespace {
constexpr int kMaxStackSize = 4096;
constexpr int kMaxSymbolSize = 1024;
constexpr absl::string_view kUnknownSymbol = "(unknown)";
}  // namespace

std::vector<void*> CurrentStackTraceImpl() {
  std::vector<void*> stacktrace(kMaxStackSize, nullptr);
  int num_frames = absl::GetStackTrace(stacktrace.data(), stacktrace.size(),
                                       /*skip_count=*/0);
  if (num_frames <= 0) {
    return {};
  }
  stacktrace.resize(num_frames);
  return stacktrace;
}

std::string SymbolizeStackTraceImpl(absl::Span<void* const> stacktrace) {
  std::string formatted_trace = "Stack trace:\n";
  for (void* function : stacktrace) {
    char symbol_name[kMaxSymbolSize];
    bool success = absl::Symbolize(function, symbol_name, sizeof(symbol_name));
    absl::StrAppendFormat(
        &formatted_trace, "    %p    %s\n", function,
        success ? absl::string_view(symbol_name) : kUnknownSymbol);
  }
  return formatted_trace;
}

std::string QuicheStackTraceImpl() {
  return SymbolizeStackTraceImpl(CurrentStackTraceImpl());
}

bool QuicheShouldRunStackTraceTestImpl() {
  void* unused[4];  // An arbitrary small number of stack frames to trace.
  int stack_traces_found =
      absl::GetStackTrace(unused, ABSL_ARRAYSIZE(unused), /*skip_count=*/0);
  // absl::GetStackTrace() always returns 0 if the current platform is
  // unsupported.
  return stack_traces_found > 0;
}

}  // namespace quiche
