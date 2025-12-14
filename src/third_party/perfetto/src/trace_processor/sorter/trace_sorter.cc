/*
 * Copyright (C) 2018 The Android Open Source Project
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

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <memory>

#include "perfetto/base/logging.h"
#include "perfetto/public/compiler.h"
#include "src/trace_processor/sorter/trace_sorter.h"
#include "src/trace_processor/sorter/trace_token_buffer.h"
#include "src/trace_processor/storage/stats.h"
#include "src/trace_processor/types/trace_processor_context.h"
#include "src/trace_processor/util/bump_allocator.h"

namespace perfetto::trace_processor {

TraceSorter::TraceSorter(TraceProcessorContext* context,
                         SortingMode sorting_mode,
                         EventHandling event_handling)
    : sorting_mode_(sorting_mode),
      storage_(context->storage.get()),
      event_handling_(event_handling) {}

TraceSorter::~TraceSorter() {
  // If trace processor encountered a fatal error, it's possible for some events
  // to have been pushed without evicting them by pushing to the next stage. Do
  // that now.
  for (auto& queue : queues_) {
    for (const auto& event : queue.events_) {
      queue.sink->OnDiscardedEvent(this, GetTokenBufferId(event));
    }
  }
}

bool TraceSorter::SetSortingMode(SortingMode sorting_mode) {
  // Early out if the new sorting mode matches the old.
  if (sorting_mode == sorting_mode_) {
    return true;
  }
  // We cannot transition back to a more relaxed mode after having left that
  // mode.
  if (sorting_mode_ != SortingMode::kDefault) {
    return false;
  }
  // We cannot change sorting mode after having extracted one or more events.
  if (latest_pushed_event_ts_ != std::numeric_limits<int64_t>::min()) {
    return false;
  }
  sorting_mode_ = sorting_mode;
  return true;
}

void TraceSorter::Queue::Sort(TraceTokenBuffer& buffer, bool use_slow_sorting) {
  PERFETTO_DCHECK(needs_sorting());
  PERFETTO_DCHECK(sort_start_idx_ < events_.size());

  // If sort_min_ts_ has been set, it will no long be max_int, and so will be
  // smaller than max_ts_.
  PERFETTO_DCHECK(sort_min_ts_ < std::numeric_limits<int64_t>::max());

  // We know that all events between [0, sort_start_idx_] are sorted. Within
  // this range, perform a bound search and find the iterator for the min
  // timestamp that broke the monotonicity. Re-sort from there to the end.
  auto sort_end = events_.begin() + static_cast<ssize_t>(sort_start_idx_);
  if (use_slow_sorting) {
    PERFETTO_DCHECK(sort_min_ts_ <= max_ts_);
    PERFETTO_DCHECK(std::is_sorted(events_.begin(), sort_end,
                                   TimestampedEvent::SlowOperatorLess{buffer}));
  } else {
    PERFETTO_DCHECK(sort_min_ts_ < max_ts_);
    PERFETTO_DCHECK(std::is_sorted(events_.begin(), sort_end));
  }
  auto sort_begin = std::lower_bound(events_.begin(), sort_end, sort_min_ts_,
                                     &TimestampedEvent::Compare);
  if (use_slow_sorting) {
    std::sort(sort_begin, events_.end(),
              TimestampedEvent::SlowOperatorLess{buffer});
  } else {
    std::sort(sort_begin, events_.end());
  }
  sort_start_idx_ = 0;
  sort_min_ts_ = 0;

  // At this point |events_| must be fully sorted
  if (use_slow_sorting) {
    PERFETTO_DCHECK(std::is_sorted(events_.begin(), events_.end(),
                                   TimestampedEvent::SlowOperatorLess{buffer}));
  } else {
    PERFETTO_DCHECK(std::is_sorted(events_.begin(), events_.end()));
  }
}

// Removes all the events in |queues_| that are earlier than the given
// packet index and moves them to the next parser stages, respecting global
// timestamp order. This function is a "extract min from N sorted queues", with
// some little cleverness: we know that events tend to be bursty, so events are
// not going to be randomly distributed on the N |queues_|.
// Upon each iteration this function finds the first two queues (if any) that
// have the oldest events, and extracts events from the 1st until hitting the
// min_ts of the 2nd. Imagine the queues are as follows:
//
//  q0           {min_ts: 10  max_ts: 30}
//  q1    {min_ts:5              max_ts: 35}
//  q2              {min_ts: 12    max_ts: 40}
//
// We know that we can extract all events from q1 until we hit ts=10 without
// looking at any other queue. After hitting ts=10, we need to re-look to all of
// them to figure out the next min-event.
// There are more suitable data structures to do this (e.g. keeping a min-heap
// to avoid re-scanning all the queues all the times) but doesn't seem worth it.
// With Android traces (that have 8 CPUs) this function accounts for ~1-3% cpu
// time in a profiler.
void TraceSorter::SortAndExtractEventsUntilAllocId(
    BumpAllocator::AllocId limit_alloc_id) {
  constexpr int64_t kTsMax = std::numeric_limits<int64_t>::max();
  for (;;) {
    size_t min_queue_idx = 0;  // The index of the queue with the min(ts).

    // The top-2 min(ts) among all queues.
    // queues_[min_queue_idx].events.timestamp == min_queue_ts[0].
    int64_t min_queue_ts[2]{kTsMax, kTsMax};

    // This loop identifies the queue which starts with the earliest event and
    // also remembers the earliest event of the 2nd queue (in min_queue_ts[1]).
    bool all_queues_empty = true;
    for (size_t i = 0; i < queues_.size(); i++) {
      auto& queue = queues_[i];
      if (queue.events_.empty()) {
        continue;
      }
      PERFETTO_DCHECK(queue.max_ts_ <= append_max_ts_);

      // Checking for |all_queues_empty| is necessary here as in fuzzer cases
      // we can end up with |int64::max()| as the value here.
      // See https://crbug.com/oss-fuzz/69164 for an example.
      if (all_queues_empty || queue.min_ts_ < min_queue_ts[0]) {
        min_queue_ts[1] = min_queue_ts[0];
        min_queue_ts[0] = queue.min_ts_;
        min_queue_idx = i;
      } else if (queue.min_ts_ < min_queue_ts[1]) {
        min_queue_ts[1] = queue.min_ts_;
      }
      all_queues_empty = false;
    }
    if (all_queues_empty) {
      break;
    }

    auto& queue = queues_[min_queue_idx];
    auto& events = queue.events_;
    if (queue.needs_sorting()) {
      queue.Sort(token_buffer_, use_slow_sorting_);
    }
    PERFETTO_DCHECK(queue.min_ts_ == events.front().ts);

    // Now that we identified the min-queue, extract all events from it until
    // we hit either: (1) the min-ts of the 2nd queue or (2) the packet index
    // limit, whichever comes first.
    size_t num_extracted = 0;
    for (auto& event : events) {
      if (event.alloc_id() >= limit_alloc_id) {
        break;
      }

      if (event.ts > min_queue_ts[1]) {
        // We should never hit this condition on the first extraction as by
        // the algorithm above (event.ts =) min_queue_ts[0] <= min_queue[1].
        PERFETTO_DCHECK(num_extracted > 0);
        break;
      }
      ++num_extracted;

      if (event.ts < latest_pushed_event_ts_) {
        storage_->IncrementStats(stats::sorter_push_event_out_of_order);
        queue.sink->OnDiscardedEvent(this, GetTokenBufferId(event));
        continue;
      }
      latest_pushed_event_ts_ = event.ts;

      if (PERFETTO_UNLIKELY(event_handling_ == EventHandling::kSortAndDrop)) {
        // Parse* would extract this event and push it to the next stage. Since
        // we are skipping that, just extract and discard it.
        queue.sink->OnDiscardedEvent(this, GetTokenBufferId(event));
        continue;
      }
      PERFETTO_DCHECK(event_handling_ == EventHandling::kSortAndPush);

      queue.sink->OnSortedEvent(this, event.ts, GetTokenBufferId(event));
    }  // for (event: events)

    // The earliest event cannot be extracted without going past the limit.
    if (!num_extracted) {
      break;
    }

    // Now remove the entries from the event buffer and update the queue-local
    // and global time bounds.
    events.erase_front(num_extracted);
    events.shrink_to_fit();

    // Since we likely just removed a bunch of items try to reduce the memory
    // usage of the token buffer.
    token_buffer_.FreeMemory();

    // Update the queue timestamps to reflect the bounds after extraction.
    if (events.empty()) {
      queue.min_ts_ = kTsMax;
      queue.max_ts_ = 0;
    } else {
      queue.min_ts_ = queue.events_.front().ts;
    }
  }  // for(;;)
}

TraceSorter::UntypedSink::~UntypedSink() = default;

}  // namespace perfetto::trace_processor
