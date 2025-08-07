/*
 * Copyright (C) 2019 The Android Open Source Project
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

#include "src/trace_processor/importers/ftrace/rss_stat_tracker.h"

#include <cstdint>
#include <optional>

#include "perfetto/base/logging.h"
#include "src/trace_processor/importers/common/event_tracker.h"
#include "src/trace_processor/importers/common/process_tracker.h"
#include "src/trace_processor/storage/stats.h"
#include "src/trace_processor/storage/trace_storage.h"
#include "src/trace_processor/types/trace_processor_context.h"

#include "protos/perfetto/trace/ftrace/ftrace_event.pbzero.h"
#include "protos/perfetto/trace/ftrace/kmem.pbzero.h"
#include "protos/perfetto/trace/ftrace/synthetic.pbzero.h"

namespace perfetto::trace_processor {
namespace {

using FtraceEvent = protos::pbzero::FtraceEvent;

const char* GetProcessMemoryKey(uint32_t member) {
  switch (member) {
    case 0:
      return "rss.file";
    case 1:
      return "rss.anon";
    case 2:
      return "swap";
    case 3:
      return "rss.shmem";
    case 4:
      return "unreclaimable";
    default:
      return "unknown";
  }
}

}  // namespace

RssStatTracker::RssStatTracker(TraceProcessorContext* context)
    : context_(context) {}

void RssStatTracker::ParseRssStat(int64_t ts,
                                  uint32_t field_id,
                                  uint32_t pid,
                                  ConstBytes blob) {
  uint32_t member;
  int64_t size;
  std::optional<bool> curr;
  std::optional<int64_t> mm_id;

  if (field_id == FtraceEvent::kRssStatFieldNumber) {
    protos::pbzero::RssStatFtraceEvent::Decoder rss(blob);

    member = static_cast<uint32_t>(rss.member());
    size = rss.size();
    if (rss.has_curr()) {
      curr = std::make_optional(static_cast<bool>(rss.curr()));
    }
    if (rss.has_mm_id()) {
      mm_id = std::make_optional(rss.mm_id());
    }

    ParseRssStat(ts, pid, size, member, curr, mm_id);
  } else if (field_id == FtraceEvent::kRssStatThrottledFieldNumber) {
    protos::pbzero::RssStatThrottledFtraceEvent::Decoder rss(blob);

    member = static_cast<uint32_t>(rss.member());
    size = rss.size();
    curr = std::make_optional(static_cast<bool>(rss.curr()));
    mm_id = std::make_optional(rss.mm_id());

    ParseRssStat(ts, pid, size, member, curr, mm_id);
  } else {
    PERFETTO_DFATAL("Unexpected field id");
  }
}

void RssStatTracker::ParseRssStat(int64_t ts,
                                  uint32_t pid,
                                  int64_t size,
                                  uint32_t member,
                                  std::optional<bool> curr,
                                  std::optional<int64_t> mm_id) {
  const char* memory_key = GetProcessMemoryKey(member);
  if (!memory_key) {
    context_->storage->IncrementStats(stats::rss_stat_unknown_keys);
    return;
  }
  if (size < 0) {
    context_->storage->IncrementStats(stats::rss_stat_negative_size);
    return;
  }

  std::optional<UniqueTid> utid;
  if (mm_id.has_value() && curr.has_value()) {
    utid = FindUtidForMmId(*mm_id, *curr, pid);
  } else {
    utid = context_->process_tracker->GetOrCreateThread(pid);
  }

  if (utid) {
    context_->event_tracker->PushProcessCounterForThread(
        EventTracker::RssStat{memory_key}, ts, static_cast<double>(size),
        *utid);
  } else {
    context_->storage->IncrementStats(stats::rss_stat_unknown_thread_for_mm_id);
  }
}

std::optional<UniqueTid> RssStatTracker::FindUtidForMmId(int64_t mm_id,
                                                         bool is_curr,
                                                         uint32_t pid) {
  // If curr is true, we can just overwrite the state in the map and return
  // the utid correspodning to |pid|.
  if (is_curr) {
    UniqueTid utid = context_->process_tracker->GetOrCreateThread(pid);
    mm_id_to_utid_[mm_id] = utid;
    return utid;
  }

  // If curr is false, try and lookup the utid we previously saw for this
  // mm id.
  auto* it = mm_id_to_utid_.Find(mm_id);
  if (!it)
    return std::nullopt;

  // If the utid in the map is the same as our current utid but curr is false,
  // that means we are in the middle of a process changing mm structs (i.e. in
  // the middle of a vfork + exec). Therefore, we should discard the association
  // of this vm struct with this thread.
  const UniqueTid mm_utid = *it;
  const UniqueTid utid = context_->process_tracker->GetOrCreateThread(pid);
  if (mm_utid == utid) {
    mm_id_to_utid_.Erase(mm_id);
    return std::nullopt;
  }

  // Verify that the utid in the map is still alive. This can happen if an mm
  // struct we saw in the past is about to be reused after thread but we don't
  // know the new process that struct will be associated with.
  if (!context_->process_tracker->IsThreadAlive(mm_utid)) {
    mm_id_to_utid_.Erase(mm_id);
    return std::nullopt;
  }

  // This case happens when a process is changing the VM of another process and
  // we know that the utid corresponding to the target process. Just return that
  // utid.
  return mm_utid;
}

}  // namespace perfetto::trace_processor
