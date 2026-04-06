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

#include "src/trace_processor/importers/ftrace/ftrace_sched_event_tracker.h"

#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>

#include "perfetto/base/logging.h"
#include "perfetto/ext/base/string_view.h"
#include "perfetto/public/compiler.h"
#include "src/trace_processor/importers/common/args_tracker.h"
#include "src/trace_processor/importers/common/event_tracker.h"
#include "src/trace_processor/importers/common/process_tracker.h"
#include "src/trace_processor/importers/common/sched_event_state.h"
#include "src/trace_processor/importers/common/sched_event_tracker.h"
#include "src/trace_processor/importers/common/system_info_tracker.h"
#include "src/trace_processor/importers/common/thread_state_tracker.h"
#include "src/trace_processor/importers/ftrace/ftrace_descriptors.h"
#include "src/trace_processor/storage/stats.h"
#include "src/trace_processor/storage/trace_storage.h"
#include "src/trace_processor/tables/metadata_tables_py.h"
#include "src/trace_processor/types/task_state.h"
#include "src/trace_processor/types/trace_processor_context.h"
#include "src/trace_processor/types/variadic.h"

#include "protos/perfetto/trace/ftrace/ftrace_event.pbzero.h"
#include "protos/perfetto/trace/ftrace/sched.pbzero.h"
#include "src/trace_processor/types/version_number.h"

namespace perfetto::trace_processor {

FtraceSchedEventTracker::FtraceSchedEventTracker(TraceProcessorContext* context)
    : context_(context) {
  // pre-parse sched_switch
  auto* switch_descriptor = GetMessageDescriptorForId(
      protos::pbzero::FtraceEvent::kSchedSwitchFieldNumber);
  PERFETTO_CHECK(switch_descriptor->max_field_id == kSchedSwitchMaxFieldId);

  for (size_t i = 1; i <= kSchedSwitchMaxFieldId; i++) {
    sched_switch_field_ids_[i] =
        context->storage->InternString(switch_descriptor->fields[i].name);
  }
  sched_switch_id_ = context->storage->InternString(switch_descriptor->name);

  // pre-parse sched_waking
  auto* waking_descriptor = GetMessageDescriptorForId(
      protos::pbzero::FtraceEvent::kSchedWakingFieldNumber);
  PERFETTO_CHECK(waking_descriptor->max_field_id == kSchedWakingMaxFieldId);

  for (size_t i = 1; i <= kSchedWakingMaxFieldId; i++) {
    sched_waking_field_ids_[i] =
        context->storage->InternString(waking_descriptor->fields[i].name);
  }
  sched_waking_id_ = context->storage->InternString(waking_descriptor->name);
}

FtraceSchedEventTracker::~FtraceSchedEventTracker() = default;

void FtraceSchedEventTracker::PushSchedSwitch(uint32_t cpu,
                                              int64_t ts,
                                              int64_t prev_pid,
                                              base::StringView prev_comm,
                                              int32_t prev_prio,
                                              int64_t prev_state,
                                              int64_t next_pid,
                                              base::StringView next_comm,
                                              int32_t next_prio) {
  StringId next_comm_id = context_->storage->InternString(next_comm);
  UniqueTid next_utid = context_->process_tracker->GetOrCreateThread(next_pid);
  context_->process_tracker->UpdateThreadName(next_utid, next_comm_id,
                                              ThreadNamePriority::kFtrace);

  // First use this data to close the previous slice.
  bool prev_pid_match_prev_next_pid = false;
  auto* pending_sched = sched_event_state_.GetPendingSchedInfoForCpu(cpu);
  uint32_t pending_slice_idx = pending_sched->pending_slice_storage_idx;
  StringId prev_state_string_id = TaskStateToStringId(prev_state);
  if (prev_state_string_id == kNullStringId) {
    context_->storage->IncrementStats(stats::task_state_invalid);
  }
  if (pending_slice_idx < std::numeric_limits<uint32_t>::max()) {
    prev_pid_match_prev_next_pid = prev_pid == pending_sched->last_pid;
    if (PERFETTO_LIKELY(prev_pid_match_prev_next_pid)) {
      context_->sched_event_tracker->ClosePendingSlice(pending_slice_idx, ts,
                                                       prev_state_string_id);
    } else {
      // If the pids are not consistent, make a note of this.
      context_->storage->IncrementStats(stats::mismatched_sched_switch_tids);
    }
  }

  // We have to intern prev_comm again because our assumption that
  // this event's |prev_comm| == previous event's |next_comm| does not hold
  // if the thread changed its name while scheduled.
  StringId prev_comm_id = context_->storage->InternString(prev_comm);
  UniqueTid prev_utid = context_->process_tracker->GetOrCreateThread(prev_pid);
  context_->process_tracker->UpdateThreadName(prev_utid, prev_comm_id,
                                              ThreadNamePriority::kFtrace);

  AddRawSchedSwitchEvent(cpu, ts, prev_utid, prev_pid, prev_comm_id, prev_prio,
                         prev_state, next_pid, next_comm_id, next_prio);

  auto new_slice_idx = context_->sched_event_tracker->AddStartSlice(
      cpu, ts, next_utid, next_prio);

  // Finally, update the info for the next sched switch on this CPU.
  pending_sched->pending_slice_storage_idx = new_slice_idx;
  pending_sched->last_pid = next_pid;
  pending_sched->last_utid = next_utid;
  pending_sched->last_prio = next_prio;

  // Update the ThreadState table.
  ThreadStateTracker::GetOrCreate(context_)->PushSchedSwitchEvent(
      ts, cpu, prev_utid, prev_state_string_id, next_utid);
}

void FtraceSchedEventTracker::PushSchedSwitchCompact(uint32_t cpu,
                                                     int64_t ts,
                                                     int64_t prev_state,
                                                     int64_t next_pid,
                                                     int32_t next_prio,
                                                     StringId next_comm_id,
                                                     bool parse_only_into_raw) {
  UniqueTid next_utid = context_->process_tracker->GetOrCreateThread(next_pid);
  context_->process_tracker->UpdateThreadName(next_utid, next_comm_id,
                                              ThreadNamePriority::kFtrace);

  // If we're processing the first compact event for this cpu, don't start a
  // slice since we're missing the "prev_*" fields. The successive events will
  // create slices as normal, but the first per-cpu switch is effectively
  // discarded.
  auto* pending_sched = sched_event_state_.GetPendingSchedInfoForCpu(cpu);
  if (pending_sched->last_utid == std::numeric_limits<UniqueTid>::max()) {
    context_->storage->IncrementStats(stats::compact_sched_switch_skipped);

    pending_sched->last_pid = next_pid;
    pending_sched->last_utid = next_utid;
    pending_sched->last_prio = next_prio;
    // Note: no pending slice, so leave |pending_slice_storage_idx| in its
    // invalid state.
    return;
  }

  // Close the pending slice if any (we won't have one when processing the first
  // two compact events for a given cpu).
  uint32_t pending_slice_idx = pending_sched->pending_slice_storage_idx;
  StringId prev_state_str_id = TaskStateToStringId(prev_state);
  if (prev_state_str_id == kNullStringId) {
    context_->storage->IncrementStats(stats::task_state_invalid);
  }
  if (pending_slice_idx != std::numeric_limits<uint32_t>::max()) {
    context_->sched_event_tracker->ClosePendingSlice(pending_slice_idx, ts,
                                                     prev_state_str_id);
  }

  // Use the previous event's values to infer this event's "prev_*" fields.
  // There are edge cases, but this assumption should still produce sensible
  // results in the absence of data loss.
  UniqueTid prev_utid = pending_sched->last_utid;
  int64_t prev_pid = pending_sched->last_pid;
  int32_t prev_prio = pending_sched->last_prio;

  // Do a fresh task name lookup in case it was updated by a task_rename while
  // scheduled.
  StringId prev_comm_id =
      context_->storage->thread_table()[prev_utid].name().value_or(
          kNullStringId);

  AddRawSchedSwitchEvent(cpu, ts, prev_utid, prev_pid, prev_comm_id, prev_prio,
                         prev_state, next_pid, next_comm_id, next_prio);

  // Update the info for the next sched switch on this CPU.
  pending_sched->last_pid = next_pid;
  pending_sched->last_utid = next_utid;
  pending_sched->last_prio = next_prio;

  // Subtle: if only inserting into raw, we're ending with:
  // * updated |pending_sched->last_*| fields
  // * still-defaulted |pending_slice_storage_idx|
  // This is similar to the first compact_sched_switch per cpu.
  if (PERFETTO_UNLIKELY(parse_only_into_raw))
    return;

  // Update per-cpu Sched table.
  auto new_slice_idx = context_->sched_event_tracker->AddStartSlice(
      cpu, ts, next_utid, next_prio);
  pending_sched->pending_slice_storage_idx = new_slice_idx;

  // Update the per-thread ThreadState table.
  ThreadStateTracker::GetOrCreate(context_)->PushSchedSwitchEvent(
      ts, cpu, prev_utid, prev_state_str_id, next_utid);
}

// Processes a sched_waking that was decoded from a compact representation,
// adding to the raw and instants tables.
void FtraceSchedEventTracker::PushSchedWakingCompact(uint32_t cpu,
                                                     int64_t ts,
                                                     int64_t wakee_pid,
                                                     uint16_t target_cpu,
                                                     uint16_t prio,
                                                     StringId comm_id,
                                                     uint16_t common_flags,
                                                     bool parse_only_into_raw) {
  // We infer the task that emitted the event (i.e. common_pid) from the
  // scheduling slices. Drop the event if we haven't seen any sched_switch
  // events for this cpu yet.
  // Note that if sched_switch wasn't enabled, we will have to skip all
  // compact waking events.
  auto* pending_sched = sched_event_state_.GetPendingSchedInfoForCpu(cpu);
  if (pending_sched->last_utid == std::numeric_limits<UniqueTid>::max()) {
    context_->storage->IncrementStats(stats::compact_sched_waking_skipped);
    return;
  }
  auto curr_utid = pending_sched->last_utid;

  if (PERFETTO_LIKELY(context_->config.ingest_ftrace_in_raw_table)) {
    tables::FtraceEventTable::Row row;
    row.ts = ts;
    row.name = sched_waking_id_;
    row.utid = curr_utid;
    row.common_flags = common_flags;
    row.ucpu = context_->cpu_tracker->GetOrCreateCpu(cpu);

    // Add an entry to the raw table.
    tables::FtraceEventTable::Id id =
        context_->storage->mutable_ftrace_event_table()->Insert(row).id;

    using SW = protos::pbzero::SchedWakingFtraceEvent;
    ArgsTracker args_tracker(context_);
    auto inserter = args_tracker.AddArgsTo(id);
    auto add_raw_arg = [this, &inserter](int field_num, Variadic var) {
      StringId key = sched_waking_field_ids_[static_cast<size_t>(field_num)];
      inserter.AddArg(key, var);
    };
    add_raw_arg(SW::kCommFieldNumber, Variadic::String(comm_id));
    add_raw_arg(SW::kPidFieldNumber, Variadic::Integer(wakee_pid));
    add_raw_arg(SW::kPrioFieldNumber, Variadic::Integer(prio));
    add_raw_arg(SW::kTargetCpuFieldNumber, Variadic::Integer(target_cpu));
  }

  if (PERFETTO_UNLIKELY(parse_only_into_raw))
    return;

  // Add a waking entry to the ThreadState table.
  auto wakee_utid = context_->process_tracker->GetOrCreateThread(wakee_pid);
  ThreadStateTracker::GetOrCreate(context_)->PushWakingEvent(
      ts, wakee_utid, curr_utid, common_flags);
}

void FtraceSchedEventTracker::AddRawSchedSwitchEvent(uint32_t cpu,
                                                     int64_t ts,
                                                     UniqueTid prev_utid,
                                                     int64_t prev_pid,
                                                     StringId prev_comm_id,
                                                     int32_t prev_prio,
                                                     int64_t prev_state,
                                                     int64_t next_pid,
                                                     StringId next_comm_id,
                                                     int32_t next_prio) {
  if (PERFETTO_LIKELY(context_->config.ingest_ftrace_in_raw_table)) {
    // Push the raw event - this is done as the raw ftrace event codepath does
    // not insert sched_switch.
    auto ucpu = context_->cpu_tracker->GetOrCreateCpu(cpu);
    tables::FtraceEventTable::Id id =
        context_->storage->mutable_ftrace_event_table()
            ->Insert({ts, sched_switch_id_, prev_utid, {}, {}, ucpu})
            .id;

    // Note: this ordering is important. The events should be pushed in the same
    // order as the order of fields in the proto; this is used by the raw table
    // to index these events using the field ids.
    using SS = protos::pbzero::SchedSwitchFtraceEvent;

    ArgsTracker args_tracker(context_);
    auto inserter = args_tracker.AddArgsTo(id);
    auto add_raw_arg = [this, &inserter](int field_num, Variadic var) {
      StringId key = sched_switch_field_ids_[static_cast<size_t>(field_num)];
      inserter.AddArg(key, var);
    };
    add_raw_arg(SS::kPrevCommFieldNumber, Variadic::String(prev_comm_id));
    add_raw_arg(SS::kPrevPidFieldNumber, Variadic::Integer(prev_pid));
    add_raw_arg(SS::kPrevPrioFieldNumber, Variadic::Integer(prev_prio));
    add_raw_arg(SS::kPrevStateFieldNumber, Variadic::Integer(prev_state));
    add_raw_arg(SS::kNextCommFieldNumber, Variadic::String(next_comm_id));
    add_raw_arg(SS::kNextPidFieldNumber, Variadic::Integer(next_pid));
    add_raw_arg(SS::kNextPrioFieldNumber, Variadic::Integer(next_prio));
  }
}

StringId FtraceSchedEventTracker::TaskStateToStringId(int64_t task_state_int) {
  using ftrace_utils::TaskState;
  std::optional<VersionNumber> kernel_version =
      SystemInfoTracker::GetOrCreate(context_)->GetKernelVersion();

  TaskState task_state = TaskState::FromRawPrevState(
      static_cast<uint16_t>(task_state_int), kernel_version);
  return task_state.is_valid()
             ? context_->storage->InternString(task_state.ToString().data())
             : kNullStringId;
}

}  // namespace perfetto::trace_processor
