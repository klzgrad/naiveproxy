// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef BASE_TRACE_EVENT_HEAP_PROFILER_HEAP_DUMP_WRITER_H_
#define BASE_TRACE_EVENT_HEAP_PROFILER_HEAP_DUMP_WRITER_H_

#include <stddef.h>

#include <memory>
#include <set>
#include <unordered_map>

#include "base/base_export.h"
#include "base/macros.h"
#include "base/trace_event/heap_profiler_allocation_context.h"

namespace base {
namespace trace_event {

class HeapProfilerSerializationState;
class StackFrameDeduplicator;
class TracedValue;
class TypeNameDeduplicator;

// Aggregates |metrics_by_context|, recursively breaks down the heap, and
// returns a traced value with an "entries" array that can be dumped in the
// trace log, following the format described in https://goo.gl/KY7zVE. The
// number of entries is kept reasonable because long tails are not included.
BASE_EXPORT std::unique_ptr<TracedValue> ExportHeapDump(
    const std::unordered_map<AllocationContext, AllocationMetrics>&
        metrics_by_context,
    const HeapProfilerSerializationState& heap_profiler_serialization_state);

namespace internal {

namespace {
struct Bucket;
}

// An entry in the "entries" array as described in https://goo.gl/KY7zVE.
struct BASE_EXPORT Entry {
  size_t size;
  size_t count;

  // References a backtrace in the stack frame deduplicator. -1 means empty
  // backtrace (the root of the tree).
  int stack_frame_id;

  // References a type name in the type name deduplicator. -1 indicates that
  // the size is the cumulative size for all types (the root of the tree).
  int type_id;
};

// Comparison operator to enable putting |Entry| in a |std::set|.
BASE_EXPORT bool operator<(Entry lhs, Entry rhs);

// Serializes entries to an "entries" array in a traced value.
BASE_EXPORT std::unique_ptr<TracedValue> Serialize(const std::set<Entry>& dump);

// Helper class to dump a snapshot of an |AllocationRegister| or other heap
// bookkeeping structure into a |TracedValue|. This class is intended to be
// used as a one-shot local instance on the stack.
class BASE_EXPORT HeapDumpWriter {
 public:
  // The |stack_frame_deduplicator| and |type_name_deduplicator| are not owned.
  // The heap dump writer assumes exclusive access to them during the lifetime
  // of the dump writer. The heap dumps are broken down for allocations bigger
  // than |breakdown_threshold_bytes|.
  HeapDumpWriter(StackFrameDeduplicator* stack_frame_deduplicator,
                 TypeNameDeduplicator* type_name_deduplicator,
                 uint32_t breakdown_threshold_bytes);

  ~HeapDumpWriter();

  // Aggregates allocations to compute the total size of the heap, then breaks
  // down the heap recursively. This produces the values that should be dumped
  // in the "entries" array. The number of entries is kept reasonable because
  // long tails are not included. Use |Serialize| to convert to a traced value.
  const std::set<Entry>& Summarize(
      const std::unordered_map<AllocationContext, AllocationMetrics>&
          metrics_by_context);

 private:
  // Inserts an |Entry| for |Bucket| into |entries_|. Returns false if the
  // entry was present before, true if it was not.
  bool AddEntryForBucket(const Bucket& bucket);

  // Recursively breaks down a bucket into smaller buckets and adds entries for
  // the buckets worth dumping to |entries_|.
  void BreakDown(const Bucket& bucket);

  // The collection of entries that is filled by |Summarize|.
  std::set<Entry> entries_;

  // Helper for generating the |stackFrames| dictionary. Not owned, must outlive
  // this heap dump writer instance.
  StackFrameDeduplicator* const stack_frame_deduplicator_;

  // Helper for converting type names to IDs. Not owned, must outlive this heap
  // dump writer instance.
  TypeNameDeduplicator* const type_name_deduplicator_;

  // Minimum size of an allocation for which an allocation bucket will be
  // broken down with children.
  uint32_t breakdown_threshold_bytes_;

  DISALLOW_COPY_AND_ASSIGN(HeapDumpWriter);
};

}  // namespace internal
}  // namespace trace_event
}  // namespace base

#endif  // BASE_TRACE_EVENT_HEAP_PROFILER_HEAP_DUMP_WRITER_H_
