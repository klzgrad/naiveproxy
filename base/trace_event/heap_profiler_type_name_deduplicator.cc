// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "base/trace_event/heap_profiler_type_name_deduplicator.h"

#include <stddef.h>
#include <stdlib.h>
#include <string>
#include <utility>

#include "base/json/string_escape.h"
#include "base/strings/string_split.h"
#include "base/strings/stringprintf.h"
#include "base/trace_event/memory_usage_estimator.h"
#include "base/trace_event/trace_event.h"
#include "base/trace_event/trace_event_memory_overhead.h"

namespace base {
namespace trace_event {

TypeNameDeduplicator::TypeNameDeduplicator() {
  // A null pointer has type ID 0 ("unknown type");
  type_ids_.insert(std::make_pair(nullptr, 0));
}

TypeNameDeduplicator::~TypeNameDeduplicator() = default;

int TypeNameDeduplicator::Insert(const char* type_name) {
  auto result = type_ids_.insert(std::make_pair(type_name, 0));
  auto& elem = result.first;
  bool did_not_exist_before = result.second;

  if (did_not_exist_before) {
    // The type IDs are assigned sequentially and they are zero-based, so
    // |size() - 1| is the ID of the new element.
    elem->second = static_cast<int>(type_ids_.size() - 1);
  }

  return elem->second;
}

void TypeNameDeduplicator::AppendAsTraceFormat(std::string* out) const {
  TRACE_EVENT0(TRACE_DISABLED_BY_DEFAULT("memory-infra"),
               "TypeNameDeduplicator::AppendAsTraceFormat");
  out->append("{");  // Begin the type names dictionary.

  auto it = type_ids_.begin();
  std::string buffer;

  // Write the first entry manually; the null pointer must not be dereferenced.
  // (The first entry is the null pointer because a |std::map| is ordered.)
  it++;
  out->append("\"0\":\"[unknown]\"");

  for (; it != type_ids_.end(); it++) {
    // Type IDs in the trace are strings, write them as stringified keys of
    // a dictionary.
    SStringPrintf(&buffer, ",\"%d\":", it->second);

    // TODO(ssid): crbug.com/594803 the type name is misused for file name in
    // some cases.
    StringPiece type_info = it->first;

    // |EscapeJSONString| appends, it does not overwrite |buffer|.
    bool put_in_quotes = true;
    EscapeJSONString(type_info, put_in_quotes, &buffer);
    out->append(buffer);
  }

  out->append("}");  // End the type names dictionary.
}

void TypeNameDeduplicator::EstimateTraceMemoryOverhead(
    TraceEventMemoryOverhead* overhead) {
  size_t memory_usage = EstimateMemoryUsage(type_ids_);
  overhead->Add(TraceEventMemoryOverhead::kHeapProfilerTypeNameDeduplicator,
                sizeof(TypeNameDeduplicator) + memory_usage);
}

}  // namespace trace_event
}  // namespace base
