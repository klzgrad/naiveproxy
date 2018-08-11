// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "base/trace_event/heap_profiler_heap_dump_writer.h"

#include <stdint.h>

#include <algorithm>
#include <iterator>
#include <tuple>
#include <utility>
#include <vector>

#include "base/format_macros.h"
#include "base/logging.h"
#include "base/macros.h"
#include "base/strings/stringprintf.h"
#include "base/trace_event/heap_profiler_serialization_state.h"
#include "base/trace_event/heap_profiler_stack_frame_deduplicator.h"
#include "base/trace_event/heap_profiler_type_name_deduplicator.h"
#include "base/trace_event/trace_config.h"
#include "base/trace_event/trace_event.h"
#include "base/trace_event/trace_event_argument.h"
#include "base/trace_event/trace_log.h"

// Most of what the |HeapDumpWriter| does is aggregating detailed information
// about the heap and deciding what to dump. The Input to this process is a list
// of |AllocationContext|s and size pairs.
//
// The pairs are grouped into |Bucket|s. A bucket is a group of (context, size)
// pairs where the properties of the contexts share a prefix. (Type name is
// considered a list of length one here.) First all pairs are put into one
// bucket that represents the entire heap. Then this bucket is recursively
// broken down into smaller buckets. Each bucket keeps track of whether further
// breakdown is possible.

namespace base {
namespace trace_event {
namespace internal {
namespace {

// Denotes a property of |AllocationContext| to break down by.
enum class BreakDownMode { kByBacktrace, kByTypeName };

// A group of bytes for which the context shares a prefix.
struct Bucket {
  Bucket()
      : size(0),
        count(0),
        backtrace_cursor(0),
        is_broken_down_by_type_name(false) {}

  std::vector<std::pair<const AllocationContext*, AllocationMetrics>>
      metrics_by_context;

  // The sum of the sizes of |metrics_by_context|.
  size_t size;

  // The sum of number of allocations of |metrics_by_context|.
  size_t count;

  // The index of the stack frame that has not yet been broken down by. For all
  // elements in this bucket, the stack frames 0 up to (but not including) the
  // cursor, must be equal.
  size_t backtrace_cursor;

  // When true, the type name for all elements in this bucket must be equal.
  bool is_broken_down_by_type_name;
};

// Comparison operator to order buckets by their size.
bool operator<(const Bucket& lhs, const Bucket& rhs) {
  return lhs.size < rhs.size;
}

// Groups the allocations in the bucket by |break_by|. The buckets in the
// returned list will have |backtrace_cursor| advanced or
// |is_broken_down_by_type_name| set depending on the property to group by.
std::vector<Bucket> GetSubbuckets(const Bucket& bucket,
                                  BreakDownMode break_by) {
  std::unordered_map<const void*, Bucket> breakdown;

  if (break_by == BreakDownMode::kByBacktrace) {
    for (const auto& context_and_metrics : bucket.metrics_by_context) {
      const Backtrace& backtrace = context_and_metrics.first->backtrace;
      const StackFrame* begin = std::begin(backtrace.frames);
      const StackFrame* end = begin + backtrace.frame_count;
      const StackFrame* cursor = begin + bucket.backtrace_cursor;

      DCHECK_LE(cursor, end);

      if (cursor != end) {
        Bucket& subbucket = breakdown[cursor->value];
        subbucket.size += context_and_metrics.second.size;
        subbucket.count += context_and_metrics.second.count;
        subbucket.metrics_by_context.push_back(context_and_metrics);
        subbucket.backtrace_cursor = bucket.backtrace_cursor + 1;
        subbucket.is_broken_down_by_type_name =
            bucket.is_broken_down_by_type_name;
        DCHECK_GT(subbucket.size, 0u);
        DCHECK_GT(subbucket.count, 0u);
      }
    }
  } else if (break_by == BreakDownMode::kByTypeName) {
    if (!bucket.is_broken_down_by_type_name) {
      for (const auto& context_and_metrics : bucket.metrics_by_context) {
        const AllocationContext* context = context_and_metrics.first;
        Bucket& subbucket = breakdown[context->type_name];
        subbucket.size += context_and_metrics.second.size;
        subbucket.count += context_and_metrics.second.count;
        subbucket.metrics_by_context.push_back(context_and_metrics);
        subbucket.backtrace_cursor = bucket.backtrace_cursor;
        subbucket.is_broken_down_by_type_name = true;
        DCHECK_GT(subbucket.size, 0u);
        DCHECK_GT(subbucket.count, 0u);
      }
    }
  }

  std::vector<Bucket> buckets;
  buckets.reserve(breakdown.size());
  for (auto key_bucket : breakdown)
    buckets.push_back(key_bucket.second);

  return buckets;
}

// Breaks down the bucket by |break_by|. Returns only buckets that contribute
// more than |min_size_bytes| to the total size. The long tail is omitted.
std::vector<Bucket> BreakDownBy(const Bucket& bucket,
                                BreakDownMode break_by,
                                size_t min_size_bytes) {
  std::vector<Bucket> buckets = GetSubbuckets(bucket, break_by);

  // Ensure that |buckets| is a max-heap (the data structure, not memory heap),
  // so its front contains the largest bucket. Buckets should be iterated
  // ordered by size, but sorting the vector is overkill because the long tail
  // of small buckets will be discarded. By using a max-heap, the optimal case
  // where all but the first bucket are discarded is O(n). The worst case where
  // no bucket is discarded is doing a heap sort, which is O(n log n).
  std::make_heap(buckets.begin(), buckets.end());

  // Keep including buckets until adding one would increase the number of
  // bytes accounted for by |min_size_bytes|. The large buckets end up in
  // [it, end()), [begin(), it) is the part that contains the max-heap
  // of small buckets.
  std::vector<Bucket>::iterator it;
  for (it = buckets.end(); it != buckets.begin(); --it) {
    if (buckets.front().size < min_size_bytes)
      break;

    // Put the largest bucket in [begin, it) at |it - 1| and max-heapify
    // [begin, it - 1). This puts the next largest bucket at |buckets.front()|.
    std::pop_heap(buckets.begin(), it);
  }

  // At this point, |buckets| looks like this (numbers are bucket sizes):
  //
  // <-- max-heap of small buckets --->
  //                                  <-- large buckets by ascending size -->
  // [ 19 | 11 | 13 | 7 | 2 | 5 | ... | 83 | 89 | 97 ]
  //   ^                                ^              ^
  //   |                                |              |
  //   begin()                          it             end()

  // Discard the long tail of buckets that contribute less than a percent.
  buckets.erase(buckets.begin(), it);

  return buckets;
}

}  // namespace

bool operator<(Entry lhs, Entry rhs) {
  // There is no need to compare |size|. If the backtrace and type name are
  // equal then the sizes must be equal as well.
  return std::tie(lhs.stack_frame_id, lhs.type_id) <
         std::tie(rhs.stack_frame_id, rhs.type_id);
}

HeapDumpWriter::HeapDumpWriter(StackFrameDeduplicator* stack_frame_deduplicator,
                               TypeNameDeduplicator* type_name_deduplicator,
                               uint32_t breakdown_threshold_bytes)
    : stack_frame_deduplicator_(stack_frame_deduplicator),
      type_name_deduplicator_(type_name_deduplicator),
      breakdown_threshold_bytes_(breakdown_threshold_bytes) {}

HeapDumpWriter::~HeapDumpWriter() = default;

bool HeapDumpWriter::AddEntryForBucket(const Bucket& bucket) {
  // The contexts in the bucket are all different, but the [begin, cursor) range
  // is equal for all contexts in the bucket, and the type names are the same if
  // |is_broken_down_by_type_name| is set.
  DCHECK(!bucket.metrics_by_context.empty());

  const AllocationContext* context = bucket.metrics_by_context.front().first;

  const StackFrame* backtrace_begin = std::begin(context->backtrace.frames);
  const StackFrame* backtrace_end = backtrace_begin + bucket.backtrace_cursor;
  DCHECK_LE(bucket.backtrace_cursor, arraysize(context->backtrace.frames));

  Entry entry;
  entry.stack_frame_id =
      stack_frame_deduplicator_->Insert(backtrace_begin, backtrace_end);

  // Deduplicate the type name, or use ID -1 if type name is not set.
  entry.type_id = bucket.is_broken_down_by_type_name
                      ? type_name_deduplicator_->Insert(context->type_name)
                      : -1;

  entry.size = bucket.size;
  entry.count = bucket.count;

  auto position_and_inserted = entries_.insert(entry);
  return position_and_inserted.second;
}

void HeapDumpWriter::BreakDown(const Bucket& bucket) {
  auto by_backtrace = BreakDownBy(bucket, BreakDownMode::kByBacktrace,
                                  breakdown_threshold_bytes_);
  auto by_type_name = BreakDownBy(bucket, BreakDownMode::kByTypeName,
                                  breakdown_threshold_bytes_);

  // Insert entries for the buckets. If a bucket was not present before, it has
  // not been broken down before, so recursively continue breaking down in that
  // case. There might be multiple routes to the same entry (first break down
  // by type name, then by backtrace, or first by backtrace and then by type),
  // so a set is used to avoid dumping and breaking down entries more than once.

  for (const Bucket& subbucket : by_backtrace)
    if (AddEntryForBucket(subbucket))
      BreakDown(subbucket);

  for (const Bucket& subbucket : by_type_name)
    if (AddEntryForBucket(subbucket))
      BreakDown(subbucket);
}

const std::set<Entry>& HeapDumpWriter::Summarize(
    const std::unordered_map<AllocationContext, AllocationMetrics>&
        metrics_by_context) {
  // Start with one bucket that represents the entire heap. Iterate by
  // reference, because the allocation contexts are going to point to allocation
  // contexts stored in |metrics_by_context|.
  Bucket root_bucket;
  for (const auto& context_and_metrics : metrics_by_context) {
    DCHECK_GT(context_and_metrics.second.size, 0u);
    DCHECK_GT(context_and_metrics.second.count, 0u);
    const AllocationContext* context = &context_and_metrics.first;
    root_bucket.metrics_by_context.push_back(
        std::make_pair(context, context_and_metrics.second));
    root_bucket.size += context_and_metrics.second.size;
    root_bucket.count += context_and_metrics.second.count;
  }

  AddEntryForBucket(root_bucket);

  // Recursively break down the heap and fill |entries_| with entries to dump.
  BreakDown(root_bucket);

  return entries_;
}

std::unique_ptr<TracedValue> Serialize(const std::set<Entry>& entries) {
  std::string buffer;
  std::unique_ptr<TracedValue> traced_value(new TracedValue);

  traced_value->BeginArray("entries");

  for (const Entry& entry : entries) {
    traced_value->BeginDictionary();

    // Format size as hexadecimal string into |buffer|.
    SStringPrintf(&buffer, "%" PRIx64, static_cast<uint64_t>(entry.size));
    traced_value->SetString("size", buffer);

    SStringPrintf(&buffer, "%" PRIx64, static_cast<uint64_t>(entry.count));
    traced_value->SetString("count", buffer);

    if (entry.stack_frame_id == -1) {
      // An empty backtrace (which will have ID -1) is represented by the empty
      // string, because there is no leaf frame to reference in |stackFrames|.
      traced_value->SetString("bt", "");
    } else {
      // Format index of the leaf frame as a string, because |stackFrames| is a
      // dictionary, not an array.
      SStringPrintf(&buffer, "%i", entry.stack_frame_id);
      traced_value->SetString("bt", buffer);
    }

    // Type ID -1 (cumulative size for all types) is represented by the absence
    // of the "type" key in the dictionary.
    if (entry.type_id != -1) {
      // Format the type ID as a string.
      SStringPrintf(&buffer, "%i", entry.type_id);
      traced_value->SetString("type", buffer);
    }

    traced_value->EndDictionary();
  }

  traced_value->EndArray();  // "entries"
  return traced_value;
}

}  // namespace internal

std::unique_ptr<TracedValue> ExportHeapDump(
    const std::unordered_map<AllocationContext, AllocationMetrics>&
        metrics_by_context,
    const HeapProfilerSerializationState& heap_profiler_serialization_state) {
  TRACE_EVENT0(TRACE_DISABLED_BY_DEFAULT("memory-infra"), "ExportHeapDump");
  internal::HeapDumpWriter writer(
      heap_profiler_serialization_state.stack_frame_deduplicator(),
      heap_profiler_serialization_state.type_name_deduplicator(),
      heap_profiler_serialization_state
          .heap_profiler_breakdown_threshold_bytes());
  return Serialize(writer.Summarize(metrics_by_context));
}

}  // namespace trace_event
}  // namespace base
