/*
 * Copyright (C) 2023 The Android Open Source Project
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

#ifndef SRC_TRACE_PROCESSOR_PLUGINS_WINSCOPE_IMPORTER_SHELL_TRANSITIONS_TRACKER_H_
#define SRC_TRACE_PROCESSOR_PLUGINS_WINSCOPE_IMPORTER_SHELL_TRANSITIONS_TRACKER_H_

#include <cstdint>
#include <optional>
#include "perfetto/trace_processor/basic_types.h"
#include "src/trace_processor/importers/common/args_tracker.h"
#include "src/trace_processor/plugins/winscope_importer/winscope_proto_mapping.h"
#include "src/trace_processor/storage/trace_storage.h"
#include "src/trace_processor/types/trace_processor_context.h"

namespace perfetto::trace_processor::winscope {

// Tracks information in the transition table.
class ShellTransitionsTracker {
 public:
  explicit ShellTransitionsTracker(TraceProcessorContext*);

  // Returns the transition's inserter (same one across packets, so args merge);
  // committed when Flush() clears the transition.
  ArgsInserter& AddArgsTo(int32_t transition_id);

  void SetTimestamp(int32_t transition_id, int64_t timestamp_ns);
  void SetTimestampIfEmpty(int32_t transition_id, int64_t timestamp_ns);
  void SetTransitionType(int32_t transition_id, int32_t transition_type);
  void SetSendTime(int32_t transition_id, int64_t timestamp_ns);
  void SetDispatchTime(int32_t transition_id, int64_t timestamp_ns);
  void SetShellAbortTime(int32_t transition_id, int64_t timestamp_ns);
  void SetWmAbortTime(int32_t transition_id, int64_t timestamp_ns);
  void SetFinishTime(int32_t transition_id, int64_t finish_time_ns);
  void SetMergeTime(int32_t transition_id, int64_t merge_time_ns);
  void SetCreateTime(int32_t transition_id, int64_t create_time_ns);
  void SetHandler(int32_t transition_id, int64_t handler);
  void SetFlags(int32_t transition_id, int32_t flags);
  void SetStartTransactionId(int32_t transition_id, uint64_t transaction_id);
  void SetFinishTransactionId(int32_t transition_id, uint64_t transaction_id);

  void Flush();

 private:
  struct TransitionInfo {
    tables::WindowManagerShellTransitionsTable::Id row_id;
    std::optional<ArgsInserter> args_inserter;
  };

  TransitionInfo* GetOrInsertTransition(int32_t transition_id);

  std::optional<tables::WindowManagerShellTransitionsTable::RowReference>
  GetRowReference(int32_t transition_id);

  void SetStatusesAndDurations();

  TraceProcessorContext* context_;
  std::unordered_map<int32_t, TransitionInfo> transitions_infos_;
};

}  // namespace perfetto::trace_processor::winscope

#endif  // SRC_TRACE_PROCESSOR_PLUGINS_WINSCOPE_IMPORTER_SHELL_TRANSITIONS_TRACKER_H_
