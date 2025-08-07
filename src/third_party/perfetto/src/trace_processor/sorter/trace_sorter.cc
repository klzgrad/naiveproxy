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
#include <utility>

#include "perfetto/base/compiler.h"
#include "perfetto/base/logging.h"
#include "perfetto/public/compiler.h"
#include "perfetto/trace_processor/trace_blob_view.h"
#include "src/trace_processor/importers/android_bugreport/android_dumpstate_event.h"
#include "src/trace_processor/importers/android_bugreport/android_log_event.h"
#include "src/trace_processor/importers/art_method/art_method_event.h"
#include "src/trace_processor/importers/common/parser_types.h"
#include "src/trace_processor/importers/common/trace_parser.h"
#include "src/trace_processor/importers/fuchsia/fuchsia_record.h"
#include "src/trace_processor/importers/gecko/gecko_event.h"
#include "src/trace_processor/importers/instruments/row.h"
#include "src/trace_processor/importers/perf/record.h"
#include "src/trace_processor/importers/perf_text/perf_text_event.h"
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
      storage_(context->storage),
      event_handling_(event_handling) {
  AddMachineContext(context);
}

TraceSorter::~TraceSorter() {
  // If trace processor encountered a fatal error, it's possible for some events
  // to have been pushed without evicting them by pushing to the next stage. Do
  // that now.
  for (auto& sorter_data : sorter_data_by_machine_) {
    for (auto& queue : sorter_data.queues) {
      for (const auto& event : queue.events_) {
        ExtractAndDiscardTokenizedObject(event);
      }
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
    size_t min_machine_idx = 0;
    size_t min_queue_idx = 0;  // The index of the queue with the min(ts).

    // The top-2 min(ts) among all queues.
    // queues_[min_queue_idx].events.timestamp == min_queue_ts[0].
    int64_t min_queue_ts[2]{kTsMax, kTsMax};

    // This loop identifies the queue which starts with the earliest event and
    // also remembers the earliest event of the 2nd queue (in min_queue_ts[1]).
    bool all_queues_empty = true;
    for (size_t m = 0; m < sorter_data_by_machine_.size(); m++) {
      TraceSorterData& sorter_data = sorter_data_by_machine_[m];
      for (size_t i = 0; i < sorter_data.queues.size(); i++) {
        auto& queue = sorter_data.queues[i];
        if (queue.events_.empty())
          continue;
        PERFETTO_DCHECK(queue.max_ts_ <= append_max_ts_);

        // Checking for |all_queues_empty| is necessary here as in fuzzer cases
        // we can end up with |int64::max()| as the value here.
        // See https://crbug.com/oss-fuzz/69164 for an example.
        if (all_queues_empty || queue.min_ts_ < min_queue_ts[0]) {
          min_queue_ts[1] = min_queue_ts[0];
          min_queue_ts[0] = queue.min_ts_;
          min_queue_idx = i;
          min_machine_idx = m;
        } else if (queue.min_ts_ < min_queue_ts[1]) {
          min_queue_ts[1] = queue.min_ts_;
        }
        all_queues_empty = false;
      }
    }
    if (all_queues_empty)
      break;

    auto& sorter_data = sorter_data_by_machine_[min_machine_idx];
    auto& queue = sorter_data.queues[min_queue_idx];
    auto& events = queue.events_;
    if (queue.needs_sorting())
      queue.Sort(token_buffer_, use_slow_sorting_);
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
      MaybeExtractEvent(min_machine_idx, min_queue_idx, event);
    }  // for (event: events)

    // The earliest event cannot be extracted without going past the limit.
    if (!num_extracted)
      break;

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

void TraceSorter::ParseTracePacket(TraceProcessorContext& context,
                                   const TimestampedEvent& event) {
  TraceTokenBuffer::Id id = GetTokenBufferId(event);
  switch (event.type()) {
    case TimestampedEvent::Type::kPerfRecord:
      context.perf_record_parser->ParsePerfRecord(
          event.ts, token_buffer_.Extract<perf_importer::Record>(id));
      return;
    case TimestampedEvent::Type::kInstrumentsRow:
      context.instruments_row_parser->ParseInstrumentsRow(
          event.ts, token_buffer_.Extract<instruments_importer::Row>(id));
      return;
    case TimestampedEvent::Type::kTracePacket:
      context.proto_trace_parser->ParseTracePacket(
          event.ts, token_buffer_.Extract<TracePacketData>(id));
      return;
    case TimestampedEvent::Type::kTrackEvent:
      context.proto_trace_parser->ParseTrackEvent(
          event.ts, token_buffer_.Extract<TrackEventData>(id));
      return;
    case TimestampedEvent::Type::kFuchsiaRecord:
      context.fuchsia_record_parser->ParseFuchsiaRecord(
          event.ts, token_buffer_.Extract<FuchsiaRecord>(id));
      return;
    case TimestampedEvent::Type::kJsonValue:
      context.json_trace_parser->ParseJsonPacket(
          event.ts, token_buffer_.Extract<JsonEvent>(id));
      return;
    case TimestampedEvent::Type::kSpeRecord:
      context.spe_record_parser->ParseSpeRecord(
          event.ts, token_buffer_.Extract<TraceBlobView>(id));
      return;
    case TimestampedEvent::Type::kSystraceLine:
      context.json_trace_parser->ParseSystraceLine(
          event.ts, token_buffer_.Extract<SystraceLine>(id));
      return;
    case TimestampedEvent::Type::kAndroidDumpstateEvent:
      context.android_dumpstate_event_parser->ParseAndroidDumpstateEvent(
          event.ts, token_buffer_.Extract<AndroidDumpstateEvent>(id));
      return;
    case TimestampedEvent::Type::kAndroidLogEvent:
      context.android_log_event_parser->ParseAndroidLogEvent(
          event.ts, token_buffer_.Extract<AndroidLogEvent>(id));
      return;
    case TimestampedEvent::Type::kLegacyV8CpuProfileEvent:
      context.json_trace_parser->ParseLegacyV8ProfileEvent(
          event.ts, token_buffer_.Extract<LegacyV8CpuProfileEvent>(id));
      return;
    case TimestampedEvent::Type::kGeckoEvent:
      context.gecko_trace_parser->ParseGeckoEvent(
          event.ts, token_buffer_.Extract<gecko_importer::GeckoEvent>(id));
      return;
    case TimestampedEvent::Type::kArtMethodEvent:
      context.art_method_parser->ParseArtMethodEvent(
          event.ts, token_buffer_.Extract<art_method::ArtMethodEvent>(id));
      return;
    case TimestampedEvent::Type::kPerfTextEvent:
      context.perf_text_parser->ParsePerfTextEvent(
          event.ts,
          token_buffer_.Extract<perf_text_importer::PerfTextEvent>(id));
      return;
    case TimestampedEvent::Type::kInlineSchedSwitch:
    case TimestampedEvent::Type::kInlineSchedWaking:
    case TimestampedEvent::Type::kEtwEvent:
    case TimestampedEvent::Type::kFtraceEvent:
      PERFETTO_FATAL("Invalid event type");
  }
  PERFETTO_FATAL("For GCC");
}

void TraceSorter::ParseEtwPacket(TraceProcessorContext& context,
                                 uint32_t cpu,
                                 const TimestampedEvent& event) {
  TraceTokenBuffer::Id id = GetTokenBufferId(event);
  switch (static_cast<TimestampedEvent::Type>(event.event_type)) {
    case TimestampedEvent::Type::kEtwEvent:
      context.proto_trace_parser->ParseEtwEvent(
          cpu, event.ts, token_buffer_.Extract<TracePacketData>(id));
      return;
    case TimestampedEvent::Type::kInlineSchedSwitch:
    case TimestampedEvent::Type::kInlineSchedWaking:
    case TimestampedEvent::Type::kFtraceEvent:
    case TimestampedEvent::Type::kTrackEvent:
    case TimestampedEvent::Type::kSpeRecord:
    case TimestampedEvent::Type::kSystraceLine:
    case TimestampedEvent::Type::kTracePacket:
    case TimestampedEvent::Type::kPerfRecord:
    case TimestampedEvent::Type::kInstrumentsRow:
    case TimestampedEvent::Type::kJsonValue:
    case TimestampedEvent::Type::kFuchsiaRecord:
    case TimestampedEvent::Type::kAndroidDumpstateEvent:
    case TimestampedEvent::Type::kAndroidLogEvent:
    case TimestampedEvent::Type::kLegacyV8CpuProfileEvent:
    case TimestampedEvent::Type::kGeckoEvent:
    case TimestampedEvent::Type::kArtMethodEvent:
    case TimestampedEvent::Type::kPerfTextEvent:
      PERFETTO_FATAL("Invalid event type");
  }
  PERFETTO_FATAL("For GCC");
}

void TraceSorter::ParseFtracePacket(TraceProcessorContext& context,
                                    uint32_t cpu,
                                    const TimestampedEvent& event) {
  TraceTokenBuffer::Id id = GetTokenBufferId(event);
  switch (static_cast<TimestampedEvent::Type>(event.event_type)) {
    case TimestampedEvent::Type::kInlineSchedSwitch:
      context.proto_trace_parser->ParseInlineSchedSwitch(
          cpu, event.ts, token_buffer_.Extract<InlineSchedSwitch>(id));
      return;
    case TimestampedEvent::Type::kInlineSchedWaking:
      context.proto_trace_parser->ParseInlineSchedWaking(
          cpu, event.ts, token_buffer_.Extract<InlineSchedWaking>(id));
      return;
    case TimestampedEvent::Type::kFtraceEvent:
      context.proto_trace_parser->ParseFtraceEvent(
          cpu, event.ts, token_buffer_.Extract<TracePacketData>(id));
      return;
    case TimestampedEvent::Type::kEtwEvent:
    case TimestampedEvent::Type::kTrackEvent:
    case TimestampedEvent::Type::kSpeRecord:
    case TimestampedEvent::Type::kSystraceLine:
    case TimestampedEvent::Type::kTracePacket:
    case TimestampedEvent::Type::kPerfRecord:
    case TimestampedEvent::Type::kInstrumentsRow:
    case TimestampedEvent::Type::kJsonValue:
    case TimestampedEvent::Type::kFuchsiaRecord:
    case TimestampedEvent::Type::kAndroidDumpstateEvent:
    case TimestampedEvent::Type::kAndroidLogEvent:
    case TimestampedEvent::Type::kLegacyV8CpuProfileEvent:
    case TimestampedEvent::Type::kGeckoEvent:
    case TimestampedEvent::Type::kArtMethodEvent:
    case TimestampedEvent::Type::kPerfTextEvent:
      PERFETTO_FATAL("Invalid event type");
  }
  PERFETTO_FATAL("For GCC");
}

void TraceSorter::ExtractAndDiscardTokenizedObject(
    const TimestampedEvent& event) {
  TraceTokenBuffer::Id id = GetTokenBufferId(event);
  switch (static_cast<TimestampedEvent::Type>(event.event_type)) {
    case TimestampedEvent::Type::kTracePacket:
    case TimestampedEvent::Type::kFtraceEvent:
    case TimestampedEvent::Type::kEtwEvent:
      base::ignore_result(token_buffer_.Extract<TracePacketData>(id));
      return;
    case TimestampedEvent::Type::kTrackEvent:
      base::ignore_result(token_buffer_.Extract<TrackEventData>(id));
      return;
    case TimestampedEvent::Type::kFuchsiaRecord:
      base::ignore_result(token_buffer_.Extract<FuchsiaRecord>(id));
      return;
    case TimestampedEvent::Type::kJsonValue:
      base::ignore_result(token_buffer_.Extract<JsonEvent>(id));
      return;
    case TimestampedEvent::Type::kSpeRecord:
      base::ignore_result(token_buffer_.Extract<TraceBlobView>(id));
      return;
    case TimestampedEvent::Type::kSystraceLine:
      base::ignore_result(token_buffer_.Extract<SystraceLine>(id));
      return;
    case TimestampedEvent::Type::kInlineSchedSwitch:
      base::ignore_result(token_buffer_.Extract<InlineSchedSwitch>(id));
      return;
    case TimestampedEvent::Type::kInlineSchedWaking:
      base::ignore_result(token_buffer_.Extract<InlineSchedWaking>(id));
      return;
    case TimestampedEvent::Type::kPerfRecord:
      base::ignore_result(token_buffer_.Extract<perf_importer::Record>(id));
      return;
    case TimestampedEvent::Type::kInstrumentsRow:
      base::ignore_result(token_buffer_.Extract<instruments_importer::Row>(id));
      return;
    case TimestampedEvent::Type::kAndroidDumpstateEvent:
      base::ignore_result(token_buffer_.Extract<AndroidDumpstateEvent>(id));
      return;
    case TimestampedEvent::Type::kAndroidLogEvent:
      base::ignore_result(token_buffer_.Extract<AndroidLogEvent>(id));
      return;
    case TimestampedEvent::Type::kLegacyV8CpuProfileEvent:
      base::ignore_result(token_buffer_.Extract<LegacyV8CpuProfileEvent>(id));
      return;
    case TimestampedEvent::Type::kGeckoEvent:
      base::ignore_result(
          token_buffer_.Extract<gecko_importer::GeckoEvent>(id));
      return;
    case TimestampedEvent::Type::kArtMethodEvent:
      base::ignore_result(
          token_buffer_.Extract<art_method::ArtMethodEvent>(id));
      return;
    case TimestampedEvent::Type::kPerfTextEvent:
      base::ignore_result(
          token_buffer_.Extract<perf_text_importer::PerfTextEvent>(id));
      return;
  }
  PERFETTO_FATAL("For GCC");
}

void TraceSorter::MaybeExtractEvent(size_t min_machine_idx,
                                    size_t queue_idx,
                                    const TimestampedEvent& event) {
  auto* machine_context =
      sorter_data_by_machine_[min_machine_idx].machine_context;
  int64_t timestamp = event.ts;
  if (timestamp < latest_pushed_event_ts_) {
    storage_->IncrementStats(stats::sorter_push_event_out_of_order);
    ExtractAndDiscardTokenizedObject(event);
    return;
  }

  latest_pushed_event_ts_ = std::max(latest_pushed_event_ts_, timestamp);

  if (PERFETTO_UNLIKELY(event_handling_ == EventHandling::kSortAndDrop)) {
    // Parse* would extract this event and push it to the next stage. Since we
    // are skipping that, just extract and discard it.
    ExtractAndDiscardTokenizedObject(event);
    return;
  }
  PERFETTO_DCHECK(event_handling_ == EventHandling::kSortAndPush);

  if (queue_idx == 0) {
    ParseTracePacket(*machine_context, event);
  } else {
    // Ftrace queues start at offset 1. So queues_[1] = cpu[0] and so on.
    uint32_t cpu = static_cast<uint32_t>(queue_idx - 1);
    auto event_type = static_cast<TimestampedEvent::Type>(event.event_type);

    if (event_type == TimestampedEvent::Type::kEtwEvent) {
      ParseEtwPacket(*machine_context, static_cast<uint32_t>(cpu), event);
    } else {
      ParseFtracePacket(*machine_context, cpu, event);
    }
  }
}

}  // namespace perfetto::trace_processor
