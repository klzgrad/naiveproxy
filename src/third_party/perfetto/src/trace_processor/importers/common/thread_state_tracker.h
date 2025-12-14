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

#ifndef SRC_TRACE_PROCESSOR_IMPORTERS_COMMON_THREAD_STATE_TRACKER_H_
#define SRC_TRACE_PROCESSOR_IMPORTERS_COMMON_THREAD_STATE_TRACKER_H_

#include "src/trace_processor/storage/trace_storage.h"
#include "src/trace_processor/types/destructible.h"
#include "src/trace_processor/types/trace_processor_context.h"

namespace perfetto {
namespace trace_processor {

// Responsible for filling the Thread State table by analysing sched switches,
// waking events and blocking reasons.
class ThreadStateTracker : public Destructible {
 public:
  explicit ThreadStateTracker(TraceProcessorContext*);
  ThreadStateTracker(const ThreadStateTracker&) = delete;
  ThreadStateTracker& operator=(const ThreadStateTracker&) = delete;
  ~ThreadStateTracker() override;
  static ThreadStateTracker* GetOrCreate(TraceProcessorContext* context) {
    if (!context->thread_state_tracker) {
      context->thread_state_tracker.reset(new ThreadStateTracker(context));
    }
    return static_cast<ThreadStateTracker*>(
        context->thread_state_tracker.get());
  }

  // Depending on the kernel implementation thread state changes could be
  // captured by either kernel system events (e.g. context switches,
  // task waking) or by thread state change events emitted by the kernel
  // directly. These below methods:
  //
  // - PushSchedSwitchEvent
  // - PushWakingEvent
  // - PushNewTaskEvent
  // - PushBlockedReason
  //
  // Are for kernels which capture system events-level data from which we
  // could extract thread state changes to update the thread state track.

  // Will cause addition of state and update of the previous state for next_utid
  // and prev_utid.
  void PushSchedSwitchEvent(int64_t event_ts,
                            uint32_t cpu,
                            UniqueTid prev_utid,
                            StringId prev_state,
                            UniqueTid next_utid);

  // Will add a runnable state for utid and close the previously blocked one.
  void PushWakingEvent(int64_t event_ts,
                       UniqueTid utid,
                       UniqueTid waker_utid,
                       std::optional<uint16_t> common_flags = std::nullopt);

  // Will add a runnable state for utid. For a new task there are no previous
  // states to close.
  void PushNewTaskEvent(int64_t event_ts, UniqueTid utid, UniqueTid waker_utid);

  // Updates the current blocked state for utid with blocked reason.
  void PushBlockedReason(UniqueTid utid,
                         std::optional<bool> io_wait,
                         std::optional<StringId> blocked_function);

  // Kernels which emit thread state change events directly can use the methods
  // below:
  //
  // - PushThreadState
  // - UpdatePendingState
  // - GetPrevEndState
  //
  // To update the thread state track accordingly. Updating pending state is
  // necessary in this scenario because single thread state change events don't
  // capture the previous thread's end state. Therefore in scenarios like
  // context switches we update the end state after the fact.

  void PushThreadState(int64_t ts,
                       UniqueTid utid,
                       StringId state,
                       std::optional<uint16_t> cpu = std::nullopt);

  void UpdatePendingState(UniqueTid utid,
                          StringId new_state,
                          std::optional<uint16_t> cpu = std::nullopt,
                          std::optional<UniqueTid> waker_utid = std::nullopt,
                          std::optional<uint16_t> common_flags = std::nullopt);

  StringId GetPrevEndState(UniqueTid utid);

 private:
  void AddOpenState(int64_t ts,
                    UniqueTid utid,
                    StringId state,
                    std::optional<uint16_t> cpu = std::nullopt,
                    std::optional<UniqueTid> waker_utid = std::nullopt,
                    std::optional<uint16_t> common_flags = std::nullopt);
  void ClosePendingState(int64_t end_ts, UniqueTid utid, bool data_loss);

  uint32_t CommonFlagsToIrqContext(uint32_t common_flags);

  bool IsRunning(StringId state);
  bool IsBlocked(StringId state);
  bool IsRunnable(StringId state);

  bool HasPreviousRowNumbersForUtid(UniqueTid utid) {
    return utid < prev_row_numbers_for_thread_.size() &&
           prev_row_numbers_for_thread_[utid].has_value();
  }

  std::optional<tables::ThreadStateTable::RowReference> GetLastRowRef(
      UniqueTid utid);

  tables::ThreadStateTable::RowReference RowNumToRef(
      tables::ThreadStateTable::RowNumber row_number) {
    return row_number.ToRowReference(storage_->mutable_thread_state_table());
  }

  TraceStorage* const storage_;
  TraceProcessorContext* const context_;

  // Strings
  StringId running_string_id_;
  StringId runnable_string_id_;

  struct RelatedRows {
    std::optional<tables::ThreadStateTable::RowNumber> last_blocked_row;
    tables::ThreadStateTable::RowNumber last_row;
  };

  std::vector<std::optional<RelatedRows>> prev_row_numbers_for_thread_;
};
}  // namespace trace_processor
}  // namespace perfetto

#endif  // SRC_TRACE_PROCESSOR_IMPORTERS_COMMON_THREAD_STATE_TRACKER_H_
