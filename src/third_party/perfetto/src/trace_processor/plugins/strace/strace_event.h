/*
 * Copyright (C) 2026 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#ifndef SRC_TRACE_PROCESSOR_PLUGINS_STRACE_STRACE_EVENT_H_
#define SRC_TRACE_PROCESSOR_PLUGINS_STRACE_STRACE_EVENT_H_

#include <cstdint>
#include <optional>

#include "src/trace_processor/containers/string_pool.h"

namespace perfetto::trace_processor::strace_importer {

// The shape of a single strace line, w.r.t. the "<unfinished ...>"/"<...
// resumed>" markers strace prints when a syscall is interrupted (e.g. by a
// signal) and later resumed. A single enum keeps the possible states
// mutually exclusive; two independent bools could encode nonsensical
// combinations.
enum class StraceEventKind {
  // A complete, single-line call: "foo(args) = ret".
  kComplete,
  // The start of an interrupted call: "foo(args <unfinished ...>".
  kUnfinished,
  // The tail of a resumed call: "<... foo resumed> args) = ret".
  kResumed,
  // A resumed call that is itself interrupted again on the same line:
  // "<... foo resumed> args <unfinished ...>". This both ends the previous
  // (now-resumed) call and begins a new interrupted one.
  kResumedThenUnfinished,
};

// Pushed into the TraceSorter, one per strace line, in trace-timestamp
// order. Strings are interned up front (in the tokenizer) so this stays
// cheap to copy/sort.
struct alignas(8) StraceEvent {
  uint32_t tid = 0;
  StringPool::Id syscall_name_id;
  std::optional<StringPool::Id> args_id;
  std::optional<StringPool::Id> return_value_id;
  StraceEventKind kind = StraceEventKind::kComplete;
};

}  // namespace perfetto::trace_processor::strace_importer

#endif  // SRC_TRACE_PROCESSOR_PLUGINS_STRACE_STRACE_EVENT_H_
