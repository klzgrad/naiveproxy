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

#include "src/trace_processor/importers/generic_kernel/generic_kernel_module.h"

#include "src/trace_processor/importers/common/process_tracker.h"
#include "src/trace_processor/importers/common/sched_event_tracker.h"
#include "src/trace_processor/importers/common/thread_state_tracker.h"
#include "src/trace_processor/importers/common/track_tracker.h"
#include "src/trace_processor/storage/trace_storage.h"
#include "src/trace_processor/types/trace_processor_context.h"

#include "protos/perfetto/trace/generic_kernel/generic_power.pbzero.h"
#include "protos/perfetto/trace/generic_kernel/generic_task.pbzero.h"

namespace perfetto::trace_processor {

using protozero::ConstBytes;

using TaskStateEnum =
    protos::pbzero::GenericKernelTaskStateEvent::TaskStateEnum;
using PendingSchedInfo = SchedEventState::PendingSchedInfo;

PERFETTO_ALWAYS_INLINE
void GenericKernelParser::InsertPendingStateInfoForTid(
    UniqueTid utid,
    SchedEventState::PendingSchedInfo sched_info) {
  if (utid >= pending_state_per_utid_.size()) {
    pending_state_per_utid_.resize(utid + 1);
  }
  pending_state_per_utid_[utid] = sched_info;
}

PERFETTO_ALWAYS_INLINE
std::optional<SchedEventState::PendingSchedInfo>
GenericKernelParser::GetPendingStateInfoForTid(UniqueTid utid) {
  return utid < pending_state_per_utid_.size() ? pending_state_per_utid_[utid]
                                               : std::nullopt;
}

PERFETTO_ALWAYS_INLINE
void GenericKernelParser::RemovePendingStateInfoForTid(UniqueTid utid) {
  if (utid < pending_state_per_utid_.size()) {
    pending_state_per_utid_[utid].reset();
  }
}

GenericKernelParser::GenericKernelParser(TraceProcessorContext* context)
    : context_(context),
      created_string_id_(context_->storage->InternString("Created")),
      running_string_id_(context_->storage->InternString("Running")),
      dead_string_id_(context_->storage->InternString("Z")),
      destroyed_string_id_(context_->storage->InternString("X")),
      task_states_({
          context_->storage->InternString("Unknown"),
          context_->storage->InternString("Created"),
          context_->storage->InternString("R"),
          context_->storage->InternString("Running"),
          context_->storage->InternString("S"),
          context_->storage->InternString("D"),
          context_->storage->InternString("T"),
          context_->storage->InternString("Z"),
          context_->storage->InternString("X"),
      }) {}

void GenericKernelParser::ParseGenericTaskStateEvent(
    int64_t ts,
    protozero::ConstBytes data) {
  protos::pbzero::GenericKernelTaskStateEvent::Decoder task_event(data);

  StringId comm_id = context_->storage->InternString(task_event.comm());
  const uint32_t cpu = static_cast<uint32_t>(task_event.cpu());
  const int64_t tid = task_event.tid();
  const int32_t prio = task_event.prio();
  const size_t state = static_cast<size_t>(task_event.state());

  // Handle thread creation
  auto utid_opt = GenericKernelParser::GetUtidForState(ts, tid, comm_id, state);
  if (!utid_opt) {
    // Detected an invalid state event
    return;
  }

  UniqueTid utid = *utid_opt;
  StringId state_string_id = task_states_[state];

  // Given |PushSchedSwitch| updates the pending slice, run this
  // method before it.
  PendingSchedInfo prev_pending_sched =
      *sched_event_state_.GetPendingSchedInfoForCpu(cpu);

  // Handle context switches
  auto sched_switch_type =
      PushSchedSwitch(ts, cpu, tid, utid, state_string_id, prio);

  // Update the ThreadState table.
  switch (sched_switch_type) {
    case kUpdateEndState: {
      ThreadStateTracker::GetOrCreate(context_)->UpdatePendingState(
          utid, state_string_id);
      break;
    }
    case kStartWithPending: {
      ThreadStateTracker::GetOrCreate(context_)->PushThreadState(
          ts, prev_pending_sched.last_utid, kNullStringId);

      // Create the unknown thread state for the previous thread and
      // proceed to update the current thread's state.
      PERFETTO_FALLTHROUGH;
    }
    case kStart:
    case kClose:
    case kNone: {
      std::optional<uint16_t> cpu_op = state_string_id == running_string_id_
                                           ? std::optional{cpu}
                                           : std::nullopt;

      ThreadStateTracker::GetOrCreate(context_)->PushThreadState(
          ts, utid, state_string_id, cpu_op);
      break;
    }
  }
}

std::optional<UniqueTid> GenericKernelParser::GetUtidForState(int64_t ts,
                                                              int64_t tid,
                                                              StringId comm_id,
                                                              size_t state) {
  switch (state) {
    case TaskStateEnum::TASK_STATE_CREATED: {
      if (context_->process_tracker->GetThreadOrNull(tid)) {
        context_->storage->IncrementStats(
            stats::generic_task_state_invalid_order);
        return std::nullopt;
      }
      UniqueTid utid = context_->process_tracker->StartNewThread(ts, tid);
      context_->process_tracker->UpdateThreadName(
          utid, comm_id, ThreadNamePriority::kGenericKernelTask);
      return utid;
    }
    case TaskStateEnum::TASK_STATE_DESTROYED: {
      auto utid_opt = context_->process_tracker->GetThreadOrNull(tid);
      if (!utid_opt) {
        utid_opt = context_->process_tracker->GetOrCreateThread(tid);
        context_->process_tracker->UpdateThreadName(
            *utid_opt, comm_id, ThreadNamePriority::kGenericKernelTask);
      }
      context_->process_tracker->EndThread(ts, tid);
      return utid_opt;
    }
    case TaskStateEnum::TASK_STATE_DEAD:
    case TaskStateEnum::TASK_STATE_RUNNING:
    case TaskStateEnum::TASK_STATE_RUNNABLE:
    case TaskStateEnum::TASK_STATE_INTERRUPTIBLE_SLEEP:
    case TaskStateEnum::TASK_STATE_UNINTERRUPTIBLE_SLEEP:
    case TaskStateEnum::TASK_STATE_STOPPED: {
      UniqueTid utid;
      if (auto utid_opt = context_->process_tracker->GetThreadOrNull(tid);
          utid_opt) {
        StringId prev_state_id =
            ThreadStateTracker::GetOrCreate(context_)->GetPrevEndState(
                *utid_opt);
        // The only accepted state after DEAD is DESTROYED
        bool is_invalid_order = prev_state_id == dead_string_id_;
        // Consecutive RUNNING states are invalid
        is_invalid_order |= state == TaskStateEnum::TASK_STATE_RUNNING &&
                            prev_state_id == running_string_id_;
        if (is_invalid_order) {
          context_->storage->IncrementStats(
              stats::generic_task_state_invalid_order);
          return std::nullopt;
        }
        utid = *utid_opt;
      } else {
        utid = context_->process_tracker->GetOrCreateThread(tid);
      }
      context_->process_tracker->UpdateThreadName(
          utid, comm_id, ThreadNamePriority::kGenericKernelTask);
      return utid;
    }
    case TaskStateEnum::TASK_STATE_UNKNOWN:
    default: {
      context_->storage->IncrementStats(stats::task_state_invalid);
      return std::nullopt;
    }
  }
}

// Handles context switches based on GenericTaskStateEvents.
//
// Given the task state events only capture the state of a single
// task, parsing context switches becomes asynchronous because,
// the start and end events could be received in different orders.
// To manage this we need to consider both of these scenarios
// for each CPU:
//
//   start task1 -> close task1 -> start task2
//   start task1 -> start task2 -> close task1
//
// The first scenario is straightforward. For the second scenario
// we keep track of any hanging opened slices. When the closing
// event is received, we then proceed add the end_state to the
// sched_slice table.
GenericKernelParser::SchedSwitchType GenericKernelParser::PushSchedSwitch(
    int64_t ts,
    uint32_t cpu,
    int64_t tid,
    UniqueTid utid,
    StringId state_string_id,
    int32_t prio) {
  auto* pending_sched = sched_event_state_.GetPendingSchedInfoForCpu(cpu);
  uint32_t pending_slice_idx = pending_sched->pending_slice_storage_idx;
  if (state_string_id == running_string_id_) {
    auto rc = kStart;
    // Close the previous sched slice
    if (pending_slice_idx < std::numeric_limits<uint32_t>::max()) {
      context_->sched_event_tracker->ClosePendingSlice(pending_slice_idx, ts,
                                                       kNullStringId);
      InsertPendingStateInfoForTid(pending_sched->last_utid, *pending_sched);
      rc = kStartWithPending;
    }
    // Start a new sched slice for the new task.
    auto new_slice_idx =
        context_->sched_event_tracker->AddStartSlice(cpu, ts, utid, prio);

    pending_sched->pending_slice_storage_idx = new_slice_idx;
    pending_sched->last_pid = tid;
    pending_sched->last_utid = utid;
    pending_sched->last_prio = prio;
    return rc;
  }
  // Close the pending slice if applicable
  if (pending_slice_idx < std::numeric_limits<uint32_t>::max() &&
      tid == pending_sched->last_pid) {
    context_->sched_event_tracker->ClosePendingSlice(pending_slice_idx, ts,
                                                     state_string_id);
    // Clear the pending slice
    *pending_sched = SchedEventState::PendingSchedInfo();
    return kClose;
  }
  // Add end state to a previously ended context switch if applicable.
  // For the end state to be added the timestamp of the event must match
  // the timestamp of the previous context switch.
  auto hanging_sched = GetPendingStateInfoForTid(utid);
  if (hanging_sched.has_value()) {
    auto sched_slice_idx = hanging_sched->pending_slice_storage_idx;
    auto close_ts =
        context_->sched_event_tracker->GetEndTimestampForPendingSlice(
            sched_slice_idx);
    if (ts == close_ts) {
      context_->sched_event_tracker->SetEndStateForPendingSlice(
          sched_slice_idx, state_string_id);
      RemovePendingStateInfoForTid(utid);
      return kUpdateEndState;
    }
  }
  return kNone;
}

void GenericKernelParser::ParseGenericTaskRenameEvent(
    protozero::ConstBytes data) {
  protos::pbzero::GenericKernelTaskRenameEvent::Decoder task_rename_event(data);
  StringId comm = context_->storage->InternString(task_rename_event.comm());
  auto utid =
      context_->process_tracker->GetOrCreateThread(task_rename_event.tid());
  context_->process_tracker->UpdateThreadNameAndMaybeProcessName(
      utid, comm, ThreadNamePriority::kGenericKernelTask);
}

void GenericKernelParser::ParseGenericProcessTree(protozero::ConstBytes data) {
  protos::pbzero::GenericKernelProcessTree::Decoder process_tree(data);
  ProcessTracker* process_tracker = context_->process_tracker.get();

  for (auto it = process_tree.processes(); it; ++it) {
    protos::pbzero::GenericKernelProcessTree::Process::Decoder proc(*it);
    if (!proc.has_cmdline())
      continue;
    const int64_t pid = proc.pid();
    const int64_t ppid = proc.ppid();
    base::StringView cmdline = proc.cmdline();
    base::StringView name = cmdline;

    // Use argv0 as name if cmdline has spaces in it.
    size_t delim_pos = name.find(' ');
    if (delim_pos != base::StringView::npos) {
      name = name.substr(0, delim_pos);
    }

    auto pupid = process_tracker->GetOrCreateProcessWithoutMainThread(ppid);
    auto upid = process_tracker->GetOrCreateProcessWithoutMainThread(pid);

    upid = process_tracker->UpdateProcessWithParent(
        upid, pupid, /*associate_main_thread*/ false);
    process_tracker->SetProcessMetadata(upid, name, cmdline);
  }

  for (auto it = process_tree.threads(); it; ++it) {
    protos::pbzero::GenericKernelProcessTree::Thread::Decoder thread(*it);
    const int64_t pid = thread.pid();
    const int64_t tid = thread.tid();
    const bool is_main_thread = thread.is_main_thread();

    auto upid = process_tracker->GetOrCreateProcessWithoutMainThread(pid);

    auto utid = process_tracker->GetOrCreateThreadWithParent(tid, upid, false);

    process_tracker->SetMainThread(utid, is_main_thread);

    if (thread.has_comm()) {
      StringId comm_id = context_->storage->InternString(thread.comm());
      process_tracker->UpdateThreadName(utid, comm_id,
                                        ThreadNamePriority::kProcessTree);
    }
  }
}

void GenericKernelParser::ParseGenericCpuFrequencyEvent(
    int64_t ts,
    protozero::ConstBytes data) {
  protos::pbzero::GenericKernelCpuFrequencyEvent::Decoder cpu_freq_event(data);
  TrackId track = context_->track_tracker->InternTrack(
      tracks::kCpuFrequencyBlueprint, tracks::Dimensions(cpu_freq_event.cpu()));
  context_->event_tracker->PushCounter(
      ts, static_cast<double>(cpu_freq_event.freq_hz()) / 1000.0, track);
}

}  // namespace perfetto::trace_processor
