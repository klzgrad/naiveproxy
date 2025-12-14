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

#ifndef SRC_TRACE_PROCESSOR_SORTER_TRACE_SORTER_H_
#define SRC_TRACE_PROCESSOR_SORTER_TRACE_SORTER_H_

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>
#include <tuple>
#include <type_traits>
#include <utility>
#include <vector>

#include "perfetto/base/compiler.h"
#include "perfetto/base/logging.h"
#include "perfetto/ext/base/circular_queue.h"
#include "perfetto/public/compiler.h"
#include "src/trace_processor/importers/common/parser_types.h"
#include "src/trace_processor/sorter/trace_token_buffer.h"
#include "src/trace_processor/storage/stats.h"
#include "src/trace_processor/storage/trace_storage.h"
#include "src/trace_processor/types/trace_processor_context.h"
#include "src/trace_processor/util/bump_allocator.h"

namespace perfetto::trace_processor {

// This class takes care of sorting events parsed from the trace stream in
// arbitrary order and pushing them to the next pipeline stages (parsing) in
// order. In order to support streaming use-cases, sorting happens within a
// window.
//
// Events are held in the TraceSorter staging area (events_) until either:
// 1. We can determine that it's safe to extract events by observing
//  TracingServiceEvent Flush and ReadBuffer events
// 2. The trace EOF is reached
//
// Incremental extraction
//
// Incremental extraction happens by using a combination of flush and read
// buffer events from the tracing service. Note that incremental extraction
// is only applicable for write_into_file traces; ring-buffer traces will
// be sorted fully in-memory implicitly because there is only a single read
// buffer call at the end.
//
// The algorithm for incremental extraction is explained in detail at
// go/trace-sorting-is-complicated.
//
// Sorting algorithm
//
// The sorting algorithm is designed around the assumption that:
// - Most events come from ftrace.
// - Ftrace events are sorted within each cpu most of the times.
//
// Due to this, this class is oprerates as a streaming merge-sort of N+1 queues
// (N = num cpus + 1 for non-ftrace events). Each queue in turn gets sorted (if
// necessary) before proceeding with the global merge-sort-extract.
//
// When an event is pushed through, it is just appended to the end of one of
// the N queues. While appending, we keep track of the fact that the queue
// is still ordered or just lost ordering. When an out-of-order event is
// detected on a queue we keep track of: (1) the offset within the queue where
// the chaos begun, (2) the timestamp that broke the ordering.
//
// When we decide to extract events from the queues into the next stages of
// the trace processor, we re-sort the events in the queue. Rather than
// re-sorting everything all the times, we use the above knowledge to restrict
// sorting to the (hopefully smaller) tail of the |events_| staging area.
// At any time, the first partition of |events_| [0 .. sort_start_idx_) is
// ordered, and the second partition [sort_start_idx_.. end] is not.
// We use a logarithmic bound search operation to figure out what is the index
// within the first partition where sorting should start, and sort all events
// from there to the end.
class TraceSorter {
 public:
  template <typename T>
  class Stream;
  template <typename T, typename Derived>
  class Sink;

  enum class SortingMode : uint8_t {
    kDefault,
    kFullSort,
  };
  enum class EventHandling : uint8_t {
    // Indicates that events should be sorted and pushed to the parsing stage.
    kSortAndPush,

    // Indicates that events should be sorted but then dropped before pushing
    // to the parsing stage.
    // Used for performance analysis of the sorter.
    kSortAndDrop,

    // Indicates that the events should be dropped as soon as they enter the
    // sorter.
    // Used in cases where we only want to perform tokenization: dropping data
    // when it hits the sorter is much cleaner than trying to handle this
    // at every different tokenizer.
    kDrop,
  };

  TraceSorter(TraceProcessorContext*,
              SortingMode,
              EventHandling = EventHandling::kSortAndPush);

  ~TraceSorter();

  template <typename SinkType>
  std::unique_ptr<TraceSorter::Stream<typename SinkType::type>> CreateStream(
      std::unique_ptr<SinkType> sink);

  SortingMode sorting_mode() const { return sorting_mode_; }
  bool SetSortingMode(SortingMode sorting_mode);

  void ExtractEventsForced() {
    BumpAllocator::AllocId end_id = token_buffer_.PastTheEndAllocId();
    SortAndExtractEventsUntilAllocId(end_id);
    for (auto& queue : queues_) {
      PERFETTO_CHECK(queue.events_.empty());
      queue.events_ = base::CircularQueue<TimestampedEvent>();
    }

    alloc_id_for_extraction_ = end_id;
    flushes_since_extraction_ = 0;
  }

  void NotifyFlushEvent() { flushes_since_extraction_++; }

  void NotifyReadBufferEvent() {
    if (sorting_mode_ == SortingMode::kFullSort ||
        flushes_since_extraction_ < 2) {
      return;
    }

    SortAndExtractEventsUntilAllocId(alloc_id_for_extraction_);
    alloc_id_for_extraction_ = token_buffer_.PastTheEndAllocId();
    flushes_since_extraction_ = 0;
  }

  int64_t max_timestamp() const { return append_max_ts_; }

 private:
  class UntypedSink;

  struct TimestampedEvent {
    // The timestamp of this event.
    int64_t ts;

    // The fields inside BumpAllocator::AllocId of this tokenized object
    // corresponding to this event.
    uint64_t chunk_index : BumpAllocator::kChunkIndexAllocIdBits;
    uint64_t chunk_offset : BumpAllocator::kChunkOffsetAllocIdBits;

    // Indicates whether the event is a JSON event: may have special rules based
    // on the event type and duration.
    // TODO(sashwinbalaji): Update to bool.
    uint64_t is_json_event : 1;

    BumpAllocator::AllocId alloc_id() const {
      return BumpAllocator::AllocId{chunk_index, chunk_offset};
    }

    // For std::lower_bound().
    static bool Compare(const TimestampedEvent& x, int64_t ts) {
      return x.ts < ts;
    }

    // For std::sort().
    bool operator<(const TimestampedEvent& evt) const {
      return std::tie(ts, chunk_index, chunk_offset) <
             std::tie(evt.ts, evt.chunk_index, evt.chunk_offset);
    }

    struct SlowOperatorLess {
      // For std::sort() in slow mode.
      bool operator()(const TimestampedEvent& a,
                      const TimestampedEvent& b) const {
        int64_t a_key =
            a.is_json_event
                ? KeyForType(*buffer.Get<JsonEvent>(GetTokenBufferId(a)))
                : std::numeric_limits<int64_t>::max();
        int64_t b_key =
            b.is_json_event
                ? KeyForType(*buffer.Get<JsonEvent>(GetTokenBufferId(b)))
                : std::numeric_limits<int64_t>::max();
        return std::tie(a.ts, a_key, a.chunk_index, a.chunk_offset) <
               std::tie(b.ts, b_key, b.chunk_index, b.chunk_offset);
      }

      static int64_t KeyForType(const JsonEvent& event) {
        switch (event.phase) {
          case 'E':
            return std::numeric_limits<int64_t>::min();
          case 'X':
            return std::numeric_limits<int64_t>::max() - event.dur;
          default:
            return std::numeric_limits<int64_t>::max();
        }
        PERFETTO_FATAL("For GCC");
      }

      TraceTokenBuffer& buffer;
    };
  };

  static_assert(sizeof(TimestampedEvent) == 16,
                "TimestampedEvent must be equal to 16 bytes");
  static_assert(std::is_trivially_copyable_v<TimestampedEvent>,
                "TimestampedEvent must be trivially copyable");
  static_assert(std::is_trivially_move_assignable_v<TimestampedEvent>,
                "TimestampedEvent must be trivially move assignable");
  static_assert(std::is_trivially_move_constructible_v<TimestampedEvent>,
                "TimestampedEvent must be trivially move constructible");
  static_assert(std::is_nothrow_swappable_v<TimestampedEvent>,
                "TimestampedEvent must be trivially swappable");

  struct Queue {
    void Append(int64_t ts,
                TraceTokenBuffer::Id id,
                bool is_json_event,
                bool use_slow_sorting) {
      {
        events_.emplace_back();

        TimestampedEvent& event = events_.back();
        event.ts = ts;
        event.chunk_index = id.alloc_id.chunk_index;
        event.chunk_offset = id.alloc_id.chunk_offset;
        event.is_json_event = is_json_event;
      }

      // Events are often seen in order.
      if (PERFETTO_LIKELY(ts > max_ts_ ||
                          (!use_slow_sorting && ts == max_ts_))) {
        max_ts_ = ts;
      } else {
        // The event is breaking ordering. The first time it happens, keep
        // track of which index we are at. We know that everything before that
        // is sorted (because events were pushed monotonically). Everything
        // after that index, instead, will need a sorting pass before moving
        // events to the next pipeline stage.
        if (sort_start_idx_ == 0) {
          sort_start_idx_ = events_.size() - 1;
          sort_min_ts_ = ts;
        } else {
          sort_min_ts_ = std::min(sort_min_ts_, ts);
        }
      }

      min_ts_ = std::min(min_ts_, ts);
      PERFETTO_DCHECK(min_ts_ <= max_ts_);
    }

    bool needs_sorting() const { return sort_start_idx_ != 0; }
    void Sort(TraceTokenBuffer&, bool use_slow_sorting);

    base::CircularQueue<TimestampedEvent> events_;
    int64_t min_ts_ = std::numeric_limits<int64_t>::max();
    int64_t max_ts_ = 0;
    size_t sort_start_idx_ = 0;
    int64_t sort_min_ts_ = std::numeric_limits<int64_t>::max();
    std::unique_ptr<UntypedSink> sink;
  };

  void SortAndExtractEventsUntilAllocId(BumpAllocator::AllocId alloc_id);

  static TraceTokenBuffer::Id GetTokenBufferId(const TimestampedEvent& event) {
    return TraceTokenBuffer::Id{event.alloc_id()};
  }

  std::vector<Queue> queues_;

  // Whether we should ignore incremental extraction and just wait for
  // forced extractionn at the end of the trace.
  SortingMode sorting_mode_ = SortingMode::kDefault;

  TraceStorage* storage_;

  // Buffer for storing tokenized objects while the corresponding events are
  // being sorted.
  TraceTokenBuffer token_buffer_;

  // The AllocId until which events should be extracted. Set based
  // on the AllocId in |OnReadBuffer|.
  BumpAllocator::AllocId alloc_id_for_extraction_ =
      token_buffer_.PastTheEndAllocId();

  // The number of flushes which have happened since the last incremental
  // extraction.
  uint32_t flushes_since_extraction_ = 0;

  // max(e.ts for e appended to the sorter)
  int64_t append_max_ts_ = 0;

  // How events pushed into the sorter should be handled.
  EventHandling event_handling_ = EventHandling::kSortAndPush;

  // max(e.ts for e pushed to next stage)
  int64_t latest_pushed_event_ts_ = std::numeric_limits<int64_t>::min();

  // Whether when std::sorting the queues, we should use the slow
  // sorting algorithm
  bool use_slow_sorting_ = false;
};

// The non-templated base class for polymorphism.
class TraceSorter::UntypedSink {
 public:
  virtual ~UntypedSink();

  // The generic dispatch method called by the sorter.
  virtual void OnSortedEvent(TraceSorter* sorter,
                             int64_t ts,
                             TraceTokenBuffer::Id id) = 0;
  virtual void OnDiscardedEvent(TraceSorter* sorter,
                                TraceTokenBuffer::Id id) = 0;
};

// The type-safe interface that parsers implement.
template <typename T, typename Derived>
class TraceSorter::Sink : public TraceSorter::UntypedSink {
 public:
  using type = T;

  ~Sink() override = default;

  // Implements the generic dispatch method from the base class.
  void OnSortedEvent(TraceSorter* sorter,
                     int64_t ts,
                     TraceTokenBuffer::Id id) final {
    // Safely extracts the data of the expected type T...
    T data = sorter->token_buffer_.Extract<T>(id);
    // ...and calls the type-safe method on the derived class.
    static_cast<Derived*>(this)->Parse(ts, std::move(data));
  }

  void OnDiscardedEvent(TraceSorter* sorter, TraceTokenBuffer::Id id) final {
    // Safely extracts and destroys the data of the expected type T.
    T res = sorter->token_buffer_.Extract<T>(id);
    base::ignore_result(res);
  }
};

// This is the handle a tokenizer uses to push data.
template <typename T>
class TraceSorter::Stream {
 public:
  // Public API for tokenizers.
  void Push(int64_t ts, T data) {
    if (PERFETTO_UNLIKELY(sorter_->event_handling_ == EventHandling::kDrop)) {
      return;
    }
    if (PERFETTO_UNLIKELY(ts < 0)) {
      sorter_->storage_->IncrementStats(
          stats::trace_sorter_negative_timestamp_dropped);
      return;
    }
    if constexpr (std::is_same_v<T, JsonEvent>) {
      sorter_->use_slow_sorting_ =
          sorter_->use_slow_sorting_ || data.phase == 'X';
    }
    TraceTokenBuffer::Id id = sorter_->token_buffer_.Append(std::move(data));
    Queue& queue = sorter_->queues_[queue_idx_];
    queue.Append(ts, id, std::is_same_v<T, JsonEvent>,
                 sorter_->use_slow_sorting_);
    sorter_->append_max_ts_ = std::max(sorter_->append_max_ts_, queue.max_ts_);
  }

 private:
  // Only the TraceSorter can create streams.
  friend class TraceSorter;
  Stream(TraceSorter* sorter, size_t queue_idx)
      : sorter_(sorter), queue_idx_(queue_idx) {}

  TraceSorter* sorter_;
  size_t queue_idx_;
};

// The factory method implementation (in the header due to templates).
template <typename SinkType>
std::unique_ptr<TraceSorter::Stream<typename SinkType::type>>
TraceSorter::CreateStream(std::unique_ptr<SinkType> sink) {
  using T = typename SinkType::type;
  static_assert(
      std::is_base_of_v<Sink<T, SinkType>, SinkType>,
      "CreateStream sink must inherit from TraceSorter::Sink<T, Derived>");

  // 1. Allocate a new queue.
  queues_.emplace_back();
  size_t queue_idx = queues_.size() - 1;

  // 2. Move the unique_ptr, upcasting it to the base class.
  //    The queue now owns the sink.
  queues_[queue_idx].sink = std::move(sink);

  // 3. Create and return the type-safe input handle.
  return std::unique_ptr<Stream<T>>(new Stream<T>(this, queue_idx));
}

}  // namespace perfetto::trace_processor

#endif  // SRC_TRACE_PROCESSOR_SORTER_TRACE_SORTER_H_
