// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef BASE_TRACE_EVENT_HEAP_PROFILER_SERIALIZATION_STATE_H_
#define BASE_TRACE_EVENT_HEAP_PROFILER_SERIALIZATION_STATE_H_

#include <memory>
#include <set>

#include "base/base_export.h"
#include "base/trace_event/heap_profiler_stack_frame_deduplicator.h"
#include "base/trace_event/heap_profiler_type_name_deduplicator.h"
#include "base/trace_event/memory_dump_request_args.h"

namespace base {
namespace trace_event {

// Container for state variables that should be shared across all the memory
// dumps in a tracing session.
class BASE_EXPORT HeapProfilerSerializationState
    : public RefCountedThreadSafe<HeapProfilerSerializationState> {
 public:
  HeapProfilerSerializationState();

  // Returns the stack frame deduplicator that should be used by memory dump
  // providers when doing a heap dump.
  StackFrameDeduplicator* stack_frame_deduplicator() const {
    return stack_frame_deduplicator_.get();
  }

  void SetStackFrameDeduplicator(
      std::unique_ptr<StackFrameDeduplicator> stack_frame_deduplicator);

  // Returns the type name deduplicator that should be used by memory dump
  // providers when doing a heap dump.
  TypeNameDeduplicator* type_name_deduplicator() const {
    return type_name_deduplicator_.get();
  }

  void SetTypeNameDeduplicator(
      std::unique_ptr<TypeNameDeduplicator> type_name_deduplicator);

  void SetAllowedDumpModes(
      std::set<MemoryDumpLevelOfDetail> allowed_dump_modes);

  bool IsDumpModeAllowed(MemoryDumpLevelOfDetail dump_mode) const;

  void set_heap_profiler_breakdown_threshold_bytes(uint32_t value) {
    heap_profiler_breakdown_threshold_bytes_ = value;
  }

  uint32_t heap_profiler_breakdown_threshold_bytes() const {
    return heap_profiler_breakdown_threshold_bytes_;
  }

  bool is_initialized() const {
    return stack_frame_deduplicator_ && type_name_deduplicator_ &&
           heap_profiler_breakdown_threshold_bytes_;
  }

 private:
  friend class RefCountedThreadSafe<HeapProfilerSerializationState>;
  ~HeapProfilerSerializationState();

  // Deduplicates backtraces in heap dumps so they can be written once when the
  // trace is finalized.
  std::unique_ptr<StackFrameDeduplicator> stack_frame_deduplicator_;

  // Deduplicates type names in heap dumps so they can be written once when the
  // trace is finalized.
  std::unique_ptr<TypeNameDeduplicator> type_name_deduplicator_;

  uint32_t heap_profiler_breakdown_threshold_bytes_;
};

}  // namespace trace_event
}  // namespace base

#endif  // BASE_TRACE_EVENT_HEAP_PROFILER_SERIALIZATION_STATE_H
