/*
 * Copyright (C) 2024 The Android Open Source Project
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

#include "src/trace_processor/importers/archive/archive_entry.h"

#include <tuple>

namespace perfetto::trace_processor {

bool ArchiveEntry::operator<(const ArchiveEntry& rhs) const {
  auto trace_priority = [](TraceType type) -> int {
    if (type == kSymbolsTraceType)
      // Traces with symbols should be the last ones to be read.
      // TODO(carlscab): Proto traces with just ModuleSymbols packets should be
      // an exception. We actually need those are the very end (once whe have
      // all the Frames). Alternatively we could build a map address -> symbol
      // during tokenization and use this during parsing to resolve symbols.
      return 3;
    if (type == TraceType::kProtoTraceType)
      // Proto traces should always parsed first as they might contains clock
      // sync data needed to correctly parse other traces.
      return 0;
    if (type == TraceType::kGzipTraceType)
      return 1;  // Middle priority
    return 2;    // Default for other trace types
  };

  // Compare first by trace type priority, then by name,
  // and finally by index to ensure strict ordering.
  int lhs_priority = trace_priority(trace_type);
  int rhs_priority = trace_priority(rhs.trace_type);

  return std::tie(lhs_priority, name, index) <
         std::tie(rhs_priority, rhs.name, rhs.index);
}

}  // namespace perfetto::trace_processor
