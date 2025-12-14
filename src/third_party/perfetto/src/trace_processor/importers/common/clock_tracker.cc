/*
 * Copyright (C) 2025 The Android Open Source Project
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

#include "src/trace_processor/importers/common/clock_tracker.h"

#include <algorithm>
#include <cinttypes>
#include <cstdint>
#include <ctime>
#include <iterator>
#include <limits>
#include <optional>
#include <vector>

#include "perfetto/base/logging.h"
#include "perfetto/base/status.h"
#include "perfetto/ext/base/fnv_hash.h"
#include "perfetto/ext/base/status_or.h"
#include "perfetto/public/compiler.h"
#include "src/trace_processor/storage/stats.h"
#include "src/trace_processor/types/trace_processor_context.h"

#include "protos/perfetto/common/builtin_clock.pbzero.h"
#include "protos/perfetto/trace/clock_snapshot.pbzero.h"

namespace perfetto::trace_processor {

ClockSynchronizerListenerImpl::ClockSynchronizerListenerImpl(
    TraceProcessorContext* context) {
  context_ = context;
}

base::Status ClockSynchronizerListenerImpl::OnClockSyncCacheMiss() {
  context_->storage->IncrementStats(stats::clock_sync_cache_miss);
  return base::OkStatus();
}

base::Status ClockSynchronizerListenerImpl::OnInvalidClockSnapshot() {
  context_->storage->IncrementStats(stats::invalid_clock_snapshots);
  return base::OkStatus();
}

base::Status ClockSynchronizerListenerImpl::OnTraceTimeClockIdChanged(
    ClockSynchronizer<ClockSynchronizerListenerImpl>::ClockId
        trace_time_clock_id) {
  context_->metadata_tracker->SetMetadata(
      metadata::trace_time_clock_id, Variadic::Integer(trace_time_clock_id));
  return base::OkStatus();
}

base::Status ClockSynchronizerListenerImpl::OnSetTraceTimeClock(
    ClockSynchronizer<ClockSynchronizerListenerImpl>::ClockId
        trace_time_clock_id) {
  context_->metadata_tracker->SetMetadata(
      metadata::trace_time_clock_id, Variadic::Integer(trace_time_clock_id));
  return base::OkStatus();
}

// Returns true if this is a local host, false otherwise.
bool ClockSynchronizerListenerImpl::IsLocalHost() {
  return !context_->machine_id();
}

}  // namespace perfetto::trace_processor
