/*
 * Copyright (C) 2026 The Android Open Source Project
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

#ifndef SRC_TRACE_PROCESSOR_IMPORTERS_COMMON_STATS_TRACKER_H_
#define SRC_TRACE_PROCESSOR_IMPORTERS_COMMON_STATS_TRACKER_H_

#include <cstddef>
#include <cstdint>
#include <optional>

#include "src/trace_processor/importers/common/global_stats_tracker.h"
#include "src/trace_processor/types/trace_processor_context.h"

namespace perfetto::trace_processor {

// Tracks stats for a specific (machine, trace) context.
// Delegates to GlobalStatsTracker, automatically passing the context's
// machine_id and trace_id.
class StatsTracker {
 public:
  explicit StatsTracker(TraceProcessorContext* context) : context_(context) {
    // Pre-emit value=0 rows for every kMachineAndTrace kSingle stat so they
    // are visible in SQL/JSON for this (machine, trace) regardless of
    // whether anything ever writes them. See
    // GlobalStatsTracker::ZeroSingleStatsForContext.
    context_->global_stats_tracker->ZeroSingleStatsForContext(
        stats::Scope::kMachineAndTrace, context_->machine_id(),
        context_->trace_id());
  }

  void SetStats(size_t key, int64_t value) {
    context_->global_stats_tracker->SetStats(context_->machine_id(),
                                             context_->trace_id(), key, value);
  }

  void IncrementStats(size_t key, int64_t increment = 1) {
    context_->global_stats_tracker->IncrementStats(
        context_->machine_id(), context_->trace_id(), key, increment);
  }

  void SetIndexedStats(size_t key, int index, int64_t value) {
    context_->global_stats_tracker->SetIndexedStats(
        context_->machine_id(), context_->trace_id(), key, index, value);
  }

  void IncrementIndexedStats(size_t key, int index, int64_t increment = 1) {
    context_->global_stats_tracker->IncrementIndexedStats(
        context_->machine_id(), context_->trace_id(), key, index, increment);
  }

  int64_t GetStats(size_t key) const {
    return context_->global_stats_tracker->GetStats(context_->machine_id(),
                                                    context_->trace_id(), key);
  }

  std::optional<int64_t> GetIndexedStats(size_t key, int index) const {
    return context_->global_stats_tracker->GetIndexedStats(
        context_->machine_id(), context_->trace_id(), key, index);
  }

 private:
  TraceProcessorContext* const context_;
};

}  // namespace perfetto::trace_processor

#endif  // SRC_TRACE_PROCESSOR_IMPORTERS_COMMON_STATS_TRACKER_H_
