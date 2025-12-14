/*
 * Copyright (C) 2020 The Android Open Source Project
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

#include "src/trace_processor/importers/common/thread_state_tracker.h"

#include <cstdint>
#include <optional>

#include "src/trace_processor/importers/common/cpu_tracker.h"
#include "src/trace_processor/importers/common/process_tracker.h"

namespace perfetto {
namespace trace_processor {

using RowReference = tables::ThreadStateTable::RowReference;

ThreadStateTracker::ThreadStateTracker(TraceProcessorContext* context)
    : storage_(context->storage.get()),
      context_(context),
      running_string_id_(storage_->InternString("Running")),
      runnable_string_id_(storage_->InternString("R")) {}
ThreadStateTracker::~ThreadStateTracker() = default;

void ThreadStateTracker::PushSchedSwitchEvent(int64_t event_ts,
                                              uint32_t cpu,
                                              UniqueTid prev_utid,
                                              StringId prev_state,
                                              UniqueTid next_utid) {
  // Code related to previous utid. If the thread wasn't running before we know
  // we lost data and should close the slice accordingly.
  bool data_loss_cond =
      HasPreviousRowNumbersForUtid(prev_utid) &&
      !IsRunning(RowNumToRef(prev_row_numbers_for_thread_[prev_utid]->last_row)
                     .state());
  ClosePendingState(event_ts, prev_utid, data_loss_cond);
  AddOpenState(event_ts, prev_utid, prev_state);

  // Code related to next utid.
  // Due to forced migration, it is possible for the same thread to be
  // scheduled on different CPUs at the same time.
  // We work around this problem by truncating the previous state to the start
  // of this state and starting the next state normally. This is why we don't
  // check whether previous state is running/runnable. See b/186509316 for
  // details and an example on when this happens.
  ClosePendingState(event_ts, next_utid, false);
  AddOpenState(event_ts, next_utid, running_string_id_, cpu);
}

void ThreadStateTracker::PushWakingEvent(int64_t event_ts,
                                         UniqueTid utid,
                                         UniqueTid waker_utid,
                                         std::optional<uint16_t> common_flags) {
  // If thread has not had a sched switch event, just open a runnable state.
  // There's no pending state to close.
  if (!HasPreviousRowNumbersForUtid(utid)) {
    AddOpenState(event_ts, utid, runnable_string_id_, std::nullopt, waker_utid,
                 common_flags);
    return;
  }

  auto last_row_ref = RowNumToRef(prev_row_numbers_for_thread_[utid]->last_row);

  // Occasionally, it is possible to get a waking event for a thread
  // which is already in a runnable state. When this happens (or if the thread
  // is running), we just ignore the waking event. See b/186509316 for details
  // and an example on when this happens. Only blocked events can be waken up.
  if (!IsBlocked(last_row_ref.state())) {
    // If we receive a waking event while we are not blocked, we ignore this
    // in the |thread_state| table but we track in the |sched_wakeup| table.
    // The |thread_state_id| in |sched_wakeup| is the current running/runnable
    // event.
    std::optional<uint32_t> irq_context =
        common_flags
            ? std::make_optional(CommonFlagsToIrqContext(*common_flags))
            : std::nullopt;
    storage_->mutable_spurious_sched_wakeup_table()->Insert(
        {event_ts, prev_row_numbers_for_thread_[utid]->last_row.row_number(),
         irq_context, utid, waker_utid});
    return;
  }

  // Close the sleeping state and open runnable state.
  ClosePendingState(event_ts, utid, false);
  AddOpenState(event_ts, utid, runnable_string_id_, std::nullopt, waker_utid,
               common_flags);
}

void ThreadStateTracker::PushNewTaskEvent(int64_t event_ts,
                                          UniqueTid utid,
                                          UniqueTid waker_utid) {
  // open a runnable state with a non-interrupt wakeup from the cloning thread.
  AddOpenState(event_ts, utid, runnable_string_id_, /*cpu=*/std::nullopt,
               waker_utid, /*common_flags=*/0);
}

void ThreadStateTracker::PushBlockedReason(
    UniqueTid utid,
    std::optional<bool> io_wait,
    std::optional<StringId> blocked_function) {
  // Return if there is no state, as there is are no previous rows available.
  if (!HasPreviousRowNumbersForUtid(utid))
    return;

  // Return if no previous bocked row exists.
  auto blocked_row_number =
      prev_row_numbers_for_thread_[utid]->last_blocked_row;
  if (!blocked_row_number.has_value())
    return;

  auto row_reference = RowNumToRef(blocked_row_number.value());
  if (io_wait.has_value()) {
    row_reference.set_io_wait(*io_wait);
  }
  if (blocked_function.has_value()) {
    row_reference.set_blocked_function(*blocked_function);
  }
}

void ThreadStateTracker::AddOpenState(int64_t ts,
                                      UniqueTid utid,
                                      StringId state,
                                      std::optional<uint16_t> cpu,
                                      std::optional<UniqueTid> waker_utid,
                                      std::optional<uint16_t> common_flags) {
  // Ignore the swapper utid because it corresponds to the swapper thread which
  // doesn't make sense to insert.
  if (utid == context_->process_tracker->swapper_utid())
    return;

  // Insert row with unfinished state
  tables::ThreadStateTable::Row row;
  row.ts = ts;
  row.waker_utid = waker_utid;
  row.dur = -1;
  row.utid = utid;
  row.state = state;
  if (cpu)
    row.ucpu = context_->cpu_tracker->GetOrCreateCpu(*cpu);
  if (common_flags.has_value()) {
    row.irq_context = CommonFlagsToIrqContext(*common_flags);
  }

  if (waker_utid.has_value() && HasPreviousRowNumbersForUtid(*waker_utid)) {
    auto waker_row =
        RowNumToRef(prev_row_numbers_for_thread_[*waker_utid]->last_row);

    // We expect all wakers to be Running. But there are 2 cases where this
    // might not be true:
    // 1. At the start of a trace the 'waker CPU' has not yet started
    // emitting events.
    // 2. Data loss.
    if (IsRunning(waker_row.state())) {
      row.waker_id = std::make_optional(waker_row.id());
    }
  }

  auto row_num = storage_->mutable_thread_state_table()->Insert(row).row_number;

  if (utid >= prev_row_numbers_for_thread_.size()) {
    prev_row_numbers_for_thread_.resize(utid + 1);
  }

  if (!prev_row_numbers_for_thread_[utid].has_value()) {
    prev_row_numbers_for_thread_[utid] = RelatedRows{std::nullopt, row_num};
  }

  if (IsRunning(state)) {
    prev_row_numbers_for_thread_[utid] = RelatedRows{std::nullopt, row_num};
  } else if (IsBlocked(state)) {
    prev_row_numbers_for_thread_[utid] = RelatedRows{row_num, row_num};
  } else /* if (IsRunnable(state)) */ {
    prev_row_numbers_for_thread_[utid]->last_row = row_num;
  }
}

uint32_t ThreadStateTracker::CommonFlagsToIrqContext(uint32_t common_flags) {
  // If common_flags contains TRACE_FLAG_HARDIRQ | TRACE_FLAG_SOFTIRQ, wakeup
  // was emitted in interrupt context.
  // See:
  // https://cs.android.com/android/kernel/superproject/+/common-android-mainline:common/include/linux/trace_events.h
  // TODO(rsavitski): we could also include TRACE_FLAG_NMI for a complete
  // "interrupt context" meaning. But at the moment it's not necessary as this
  // is used for sched_waking events, which are not emitted from NMI contexts.
  return common_flags & (0x08 | 0x10) ? 1 : 0;
}

void ThreadStateTracker::ClosePendingState(int64_t end_ts,
                                           UniqueTid utid,
                                           bool data_loss) {
  // Discard close if there is no open state to close.
  auto row_ref = GetLastRowRef(utid);
  if (!row_ref)
    return;

  // Update the duration only for states without data loss.
  if (!data_loss) {
    row_ref->set_dur(end_ts - row_ref->ts());
  }
}

void ThreadStateTracker::PushThreadState(int64_t ts,
                                         UniqueTid utid,
                                         StringId state,
                                         std::optional<uint16_t> cpu) {
  ClosePendingState(ts, utid, false /*data_loss*/);

  if (auto row_ref = GetLastRowRef(utid); row_ref && ts == row_ref->ts()) {
    // Detected two thread state event changes at the same time.
    storage_->IncrementStats(stats::generic_task_state_invalid_order);
  }

  AddOpenState(ts, utid, state, cpu);
}

void ThreadStateTracker::UpdatePendingState(
    UniqueTid utid,
    StringId state,
    std::optional<uint16_t> cpu,
    std::optional<UniqueTid> waker_utid,
    std::optional<uint16_t> common_flags) {
  // Discard update if there is no open state to close.
  auto row_ref = GetLastRowRef(utid);
  if (!row_ref)
    return;

  row_ref->set_state(state);
  if (cpu)
    row_ref->set_ucpu(context_->cpu_tracker->GetOrCreateCpu(*cpu));
  if (waker_utid)
    row_ref->set_waker_utid(*waker_utid);
  if (common_flags.has_value()) {
    row_ref->set_irq_context(CommonFlagsToIrqContext(*common_flags));
  }
}

StringId ThreadStateTracker::GetPrevEndState(UniqueTid utid) {
  auto row_ref = GetLastRowRef(utid);
  if (!row_ref)
    return kNullStringId;

  return row_ref->state();
}

bool ThreadStateTracker::IsRunning(StringId state) {
  return state == running_string_id_;
}

bool ThreadStateTracker::IsRunnable(StringId state) {
  return state == runnable_string_id_;
}

bool ThreadStateTracker::IsBlocked(StringId state) {
  return !(IsRunnable(state) || IsRunning(state));
}

PERFETTO_ALWAYS_INLINE
std::optional<RowReference> ThreadStateTracker::GetLastRowRef(UniqueTid utid) {
  if (!HasPreviousRowNumbersForUtid(utid))
    return std::nullopt;

  return RowNumToRef(prev_row_numbers_for_thread_[utid]->last_row);
}

}  // namespace trace_processor
}  // namespace perfetto
