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

#include "src/trace_processor/importers/proto/winscope/shell_transitions_tracker.h"
#include <cstdint>
#include "src/trace_processor/types/trace_processor_context.h"

namespace perfetto::trace_processor::winscope {

ShellTransitionsTracker::ShellTransitionsTracker(TraceProcessorContext* context)
    : context_(context) {}

ArgsTracker::BoundInserter ShellTransitionsTracker::AddArgsTo(
    int32_t transition_id) {
  auto* transition_info = GetOrInsertTransition(transition_id);

  return transition_info->args_tracker.AddArgsTo(transition_info->row_id);
}

void ShellTransitionsTracker::SetTimestamp(int32_t transition_id,
                                           int64_t timestamp_ns) {
  auto row_ref = GetRowReference(transition_id);
  if (row_ref.has_value()) {
    auto row = row_ref.value();
    row.set_ts(timestamp_ns);
  }
}

void ShellTransitionsTracker::SetTimestampIfEmpty(int32_t transition_id,
                                                  int64_t timestamp_ns) {
  auto row_ref = GetRowReference(transition_id);
  if (row_ref.has_value()) {
    auto row = row_ref.value();
    if (!row.ts()) {
      row.set_ts(timestamp_ns);
    }
  }
}

void ShellTransitionsTracker::SetTransitionType(int32_t transition_id,
                                                int32_t transition_type) {
  auto row = GetRowReference(transition_id);
  if (row.has_value()) {
    row.value().set_transition_type(static_cast<uint32_t>(transition_type));
  }
}

void ShellTransitionsTracker::SetSendTime(int32_t transition_id,
                                          int64_t send_time_ns) {
  auto row = GetRowReference(transition_id);
  if (row.has_value()) {
    row.value().set_send_time_ns(send_time_ns);
    auto finish_time_ns = row->finish_time_ns();
    if (finish_time_ns.has_value()) {
      row.value().set_duration_ns(finish_time_ns.value() - send_time_ns);
    }
  }
}

void ShellTransitionsTracker::SetDispatchTime(int32_t transition_id,
                                              int64_t timestamp_ns) {
  auto row = GetRowReference(transition_id);
  if (row.has_value()) {
    row.value().set_dispatch_time_ns(timestamp_ns);
  }
}

void ShellTransitionsTracker::SetFinishTime(int32_t transition_id,
                                            int64_t finish_time_ns) {
  auto row = GetRowReference(transition_id);
  if (row.has_value()) {
    row->set_finish_time_ns(finish_time_ns);
    auto send_time_ns = row->send_time_ns();
    if (send_time_ns.has_value()) {
      row.value().set_duration_ns(finish_time_ns - send_time_ns.value());
    }
  }
}

void ShellTransitionsTracker::SetShellAbortTime(int32_t transition_id,
                                                int64_t timestamp_ns) {
  auto row = GetRowReference(transition_id);
  if (row.has_value()) {
    row.value().set_shell_abort_time_ns(timestamp_ns);
  }
}

void ShellTransitionsTracker::SetHandler(int32_t transition_id,
                                         int64_t handler) {
  auto row = GetRowReference(transition_id);
  if (row.has_value()) {
    row.value().set_handler(handler);
  }
}

void ShellTransitionsTracker::SetFlags(int32_t transition_id, int32_t flags) {
  auto row = GetRowReference(transition_id);
  if (row.has_value()) {
    row.value().set_flags(static_cast<uint32_t>(flags));
  }
}

void ShellTransitionsTracker::SetStatus(int32_t transition_id,
                                        StringPool::Id status) {
  auto row = GetRowReference(transition_id);
  if (row.has_value()) {
    row.value().set_status(status);
  }
}

void ShellTransitionsTracker::SetStartTransactionId(int32_t transition_id,
                                                    uint64_t transaction_id) {
  auto row = GetRowReference(transition_id);
  if (row.has_value()) {
    row.value().set_start_transaction_id(transaction_id);
  }
}

void ShellTransitionsTracker::SetFinishTransactionId(int32_t transition_id,
                                                     uint64_t transaction_id) {
  auto row = GetRowReference(transition_id);
  if (row.has_value()) {
    row.value().set_finish_transaction_id(transaction_id);
  }
}

void ShellTransitionsTracker::Flush() {
  // The destructor of ArgsTracker will flush the args to the tables.
  transitions_infos_.clear();
}

ShellTransitionsTracker::TransitionInfo*
ShellTransitionsTracker::GetOrInsertTransition(int32_t transition_id) {
  auto pos = transitions_infos_.find(transition_id);
  if (pos != transitions_infos_.end()) {
    return &pos->second;
  }

  auto* window_manager_shell_transitions_table =
      context_->storage->mutable_window_manager_shell_transitions_table();

  tables::WindowManagerShellTransitionsTable::Row row;
  row.transition_id = transition_id;
  auto row_id = window_manager_shell_transitions_table->Insert(row).id;

  transitions_infos_.insert(
      {transition_id, TransitionInfo{row_id, ArgsTracker(context_)}});

  pos = transitions_infos_.find(transition_id);
  return &pos->second;
}

std::optional<tables::WindowManagerShellTransitionsTable::RowReference>
ShellTransitionsTracker::GetRowReference(int32_t transition_id) {
  auto pos = transitions_infos_.find(transition_id);
  if (pos == transitions_infos_.end()) {
    context_->storage->IncrementStats(
        stats::winscope_shell_transitions_parse_errors);
    return std::nullopt;
  }

  auto* window_manager_shell_transitions_table =
      context_->storage->mutable_window_manager_shell_transitions_table();
  return window_manager_shell_transitions_table->FindById(pos->second.row_id);
}

}  // namespace perfetto::trace_processor::winscope
