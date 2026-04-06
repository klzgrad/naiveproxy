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

#ifndef SRC_TRACE_PROCESSOR_IMPORTERS_GENERIC_KERNEL_GENERIC_KERNEL_PARSER_H_
#define SRC_TRACE_PROCESSOR_IMPORTERS_GENERIC_KERNEL_GENERIC_KERNEL_PARSER_H_

#include <cstdint>

#include "perfetto/protozero/field.h"
#include "src/trace_processor/importers/common/sched_event_state.h"

namespace perfetto::trace_processor {

class TraceProcessorContext;

class GenericKernelParser {
 public:
  explicit GenericKernelParser(TraceProcessorContext* context);

  void ParseGenericTaskStateEvent(int64_t ts, protozero::ConstBytes data);

  void ParseGenericTaskRenameEvent(protozero::ConstBytes data);

  void ParseGenericProcessTree(protozero::ConstBytes data);

  void ParseGenericCpuFrequencyEvent(int64_t ts, protozero::ConstBytes data);

 private:
  enum SchedSwitchType {
    // No context switch event was handled.
    kNone = 0,
    // A new context switch slice was opened
    // without any side effects.
    kStart,
    // A new context switch slice was opened
    // and the previous running thread's slice
    // was closed without knowing the end_state.
    kStartWithPending,
    // The previously started context switch slice
    // was closed.
    kClose,
    // A closed context switch with unknown end
    // state was updated with a new valid end
    // state. No new context switch slice was
    // opened/closed.
    kUpdateEndState,
  };

  std::optional<UniqueTid> GetUtidForState(int64_t ts,
                                           int64_t tid,
                                           StringId comm_id,
                                           size_t state);

  SchedSwitchType PushSchedSwitch(int64_t ts,
                                  uint32_t cpu,
                                  int64_t tid,
                                  UniqueTid utid,
                                  StringId state_string_id,
                                  int32_t prio);

  void InsertPendingStateInfoForTid(
      UniqueTid utid,
      SchedEventState::PendingSchedInfo sched_info);

  std::optional<SchedEventState::PendingSchedInfo> GetPendingStateInfoForTid(
      UniqueTid utid);

  void RemovePendingStateInfoForTid(UniqueTid utid);

  StringId TaskStateToStringId(size_t state);

  TraceProcessorContext* context_;
  // Keeps track of the latest context switches
  SchedEventState sched_event_state_;
  std::vector<std::optional<SchedEventState::PendingSchedInfo>>
      pending_state_per_utid_;

  StringId created_string_id_;
  StringId running_string_id_;
  StringId dead_string_id_;
  StringId destroyed_string_id_;
  const std::vector<StringId> task_states_;
};

}  // namespace perfetto::trace_processor

#endif  // SRC_TRACE_PROCESSOR_IMPORTERS_GENERIC_KERNEL_GENERIC_KERNEL_PARSER_H_
