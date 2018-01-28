// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef BASE_TRACE_EVENT_HEAP_PROFILER_TYPE_NAME_DEDUPLICATOR_H_
#define BASE_TRACE_EVENT_HEAP_PROFILER_TYPE_NAME_DEDUPLICATOR_H_

#include <map>
#include <string>

#include "base/base_export.h"
#include "base/macros.h"
#include "base/trace_event/trace_event_impl.h"

namespace base {
namespace trace_event {

class TraceEventMemoryOverhead;

// Data structure that assigns a unique numeric ID to |const char*|s.
class BASE_EXPORT TypeNameDeduplicator : public ConvertableToTraceFormat {
 public:
  TypeNameDeduplicator();
  ~TypeNameDeduplicator() override;

  // Inserts a type name and returns its ID.
  int Insert(const char* type_name);

  // Writes the type ID -> type name mapping to the trace log.
  void AppendAsTraceFormat(std::string* out) const override;

  // Estimates memory overhead including |sizeof(TypeNameDeduplicator)|.
  void EstimateTraceMemoryOverhead(TraceEventMemoryOverhead* overhead) override;

 private:
  // Map from type name to type ID.
  std::map<const char*, int> type_ids_;

  DISALLOW_COPY_AND_ASSIGN(TypeNameDeduplicator);
};

}  // namespace trace_event
}  // namespace base

#endif  // BASE_TRACE_EVENT_HEAP_PROFILER_TYPE_NAME_DEDUPLICATOR_H_
