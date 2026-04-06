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
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <utility>
#include <vector>

#include "perfetto/base/logging.h"
#include "perfetto/base/status.h"
#include "perfetto/ext/base/flat_hash_map.h"
#include "perfetto/ext/base/status_or.h"
#include "perfetto/public/compiler.h"
#include "src/trace_processor/importers/common/args_tracker.h"
#include "src/trace_processor/importers/common/import_logs_tracker.h"
#include "src/trace_processor/importers/common/metadata_tracker.h"
#include "src/trace_processor/storage/metadata.h"
#include "src/trace_processor/storage/stats.h"
#include "src/trace_processor/storage/trace_storage.h"
#include "src/trace_processor/types/trace_processor_context.h"
#include "src/trace_processor/types/variadic.h"
#include "src/trace_processor/util/clock_synchronizer.h"

namespace perfetto::trace_processor {

// --- ClockTracker: public slow-path methods ---

ClockTracker::ClockTracker(
    TraceProcessorContext* context,
    std::unique_ptr<ClockSynchronizerListenerImpl> listener,
    ClockSynchronizer* primary_sync,
    bool is_primary)
    : context_(context),
      sync_(context->trace_time_state.get(), std::move(listener)),
      active_sync_(primary_sync),
      is_primary_(is_primary) {
  PERFETTO_CHECK(primary_sync);
}

base::StatusOr<uint32_t> ClockTracker::AddSnapshot(
    const std::vector<ClockTimestamp>& clock_timestamps) {
  if (PERFETTO_UNLIKELY(!is_primary_)) {
    // Non-primary trace: if we were using the primary's pool and already
    // converted timestamps, those conversions may have used different clock
    // data than our own.
    if (PERFETTO_UNLIKELY(active_sync_ != &sync_ && num_conversions_ > 0)) {
      context_->import_logs_tracker->RecordAnalysisError(
          stats::clock_sync_mixed_clock_sources,
          [&](ArgsTracker::BoundInserter& inserter) {
            StringId key = context_->storage->InternString(
                "num_conversions_using_primary");
            inserter.AddArg(key, Variadic::UnsignedInteger(num_conversions_));
          });
    }
    // Switch to our own sync and add the snapshot there.
    active_sync_ = &sync_;
  }
  return active_sync_->AddSnapshot(clock_timestamps);
}

base::Status ClockTracker::SetGlobalClock(ClockId clock_id) {
  PERFETTO_DCHECK(!clock_id.IsSequenceClock());
  if (num_conversions_ > 0) {
    auto* state = context_->trace_time_state.get();
    return base::ErrStatus(
        "Not updating trace time clock from %s to %s"
        " because the old clock was already used for timestamp "
        "conversion - ClockSnapshot too late in trace?",
        state->clock_id.ToString().c_str(), clock_id.ToString().c_str());
  }
  auto* state = context_->trace_time_state.get();
  uint32_t my_trace_id = context_->trace_id().value;
  // Another trace file owns the clock. Don't override.
  if (state->trace_time_clock_owner &&
      *state->trace_time_clock_owner != my_trace_id) {
    return base::OkStatus();
  }
  state->trace_time_clock_owner = my_trace_id;
  state->clock_id = clock_id;
  context_->metadata_tracker->SetMetadata(metadata::trace_time_clock_id,
                                          Variadic::Integer(clock_id.clock_id));
  return base::OkStatus();
}

void ClockTracker::SetTraceDefaultClock(ClockId clock_id) {
  trace_default_clock_ = clock_id;
}

void ClockTracker::AddDeferredIdentitySync(ClockId clock_id) {
  deferred_identity_clock_ = clock_id;
}

void ClockTracker::FlushDeferredIdentitySync() {
  ClockId clock_id = *deferred_identity_clock_;
  deferred_identity_clock_.reset();
  auto* state = context_->trace_time_state.get();
  if (clock_id != state->clock_id && !active_sync_->HasClock(clock_id)) {
    active_sync_->AddSnapshot({{clock_id, 0}, {state->clock_id, 0}});
  }
}

std::optional<int64_t> ClockTracker::ToTraceTimeFromSnapshot(
    const std::vector<ClockTimestamp>& snapshot) {
  auto* state = context_->trace_time_state.get();
  auto it = std::find_if(snapshot.begin(), snapshot.end(),
                         [state](const ClockTimestamp& clock_timestamp) {
                           return clock_timestamp.clock.id == state->clock_id;
                         });
  if (it == snapshot.end()) {
    return std::nullopt;
  }
  return it->timestamp;
}

void ClockTracker::SetRemoteClockOffset(ClockId clock_id, int64_t offset) {
  context_->trace_time_state->remote_clock_offsets[clock_id] = offset;
}

std::optional<int64_t> ClockTracker::timezone_offset() const {
  return context_->trace_time_state->timezone_offset;
}

void ClockTracker::set_timezone_offset(int64_t offset) {
  context_->trace_time_state->timezone_offset = offset;
}

// --- ClockTracker: testing ---

void ClockTracker::set_cache_lookups_disabled_for_testing(bool v) {
  active_sync_->set_cache_lookups_disabled_for_testing(v);
}

const base::FlatHashMap<ClockId, int64_t>&
ClockTracker::remote_clock_offsets_for_testing() {
  return context_->trace_time_state->remote_clock_offsets;
}

uint32_t ClockTracker::cache_hits_for_testing() const {
  return active_sync_->cache_hits_for_testing();
}

// --- ClockSynchronizerListenerImpl ---

ClockSynchronizerListenerImpl::ClockSynchronizerListenerImpl(
    TraceProcessorContext* context)
    : context_(context),
      source_clock_id_key_(context->storage->InternString("source_clock_id")),
      target_clock_id_key_(context->storage->InternString("target_clock_id")),
      source_timestamp_key_(context->storage->InternString("source_timestamp")),
      source_sequence_id_key_(
          context->storage->InternString("source_sequence_id")),
      target_sequence_id_key_(
          context->storage->InternString("target_sequence_id")) {}

base::Status ClockSynchronizerListenerImpl::OnClockSyncCacheMiss() {
  context_->storage->IncrementStats(stats::clock_sync_cache_miss);
  return base::OkStatus();
}

base::Status ClockSynchronizerListenerImpl::OnInvalidClockSnapshot() {
  context_->storage->IncrementStats(stats::invalid_clock_snapshots);
  return base::OkStatus();
}

void ClockSynchronizerListenerImpl::RecordConversionError(
    ClockSyncErrorType error_type,
    ClockId source_clock_id,
    ClockId target_clock_id,
    int64_t source_timestamp,
    std::optional<size_t> byte_offset) {
  size_t stat_key;
  switch (error_type) {
    case ClockSyncErrorType::kUnknownSourceClock:
      stat_key = stats::clock_sync_failure_unknown_source_clock;
      break;
    case ClockSyncErrorType::kUnknownTargetClock:
      stat_key = stats::clock_sync_failure_unknown_target_clock;
      break;
    case ClockSyncErrorType::kNoPath:
      stat_key = stats::clock_sync_failure_no_path;
      break;
    case ClockSyncErrorType::kOk:
      PERFETTO_FATAL("RecordConversionError called with kOk");
      return;
  }
  auto args = [&](ArgsTracker::BoundInserter& inserter) {
    if (source_clock_id.seq_id != 0) {
      inserter.AddArg(source_sequence_id_key_,
                      Variadic::UnsignedInteger(source_clock_id.seq_id));
      inserter.AddArg(source_clock_id_key_,
                      Variadic::Integer(source_clock_id.clock_id));
    } else {
      inserter.AddArg(source_clock_id_key_,
                      Variadic::Integer(source_clock_id.clock_id));
    }
    inserter.AddArg(source_timestamp_key_, Variadic::Integer(source_timestamp));
    if (target_clock_id.seq_id != 0) {
      inserter.AddArg(target_sequence_id_key_,
                      Variadic::UnsignedInteger(target_clock_id.seq_id));
      inserter.AddArg(target_clock_id_key_,
                      Variadic::Integer(target_clock_id.clock_id));
    } else {
      inserter.AddArg(target_clock_id_key_,
                      Variadic::Integer(target_clock_id.clock_id));
    }
  };
  if (byte_offset) {
    context_->import_logs_tracker->RecordTokenizationError(stat_key,
                                                           *byte_offset, args);
  } else {
    context_->import_logs_tracker->RecordAnalysisError(stat_key, args);
  }
}

}  // namespace perfetto::trace_processor
