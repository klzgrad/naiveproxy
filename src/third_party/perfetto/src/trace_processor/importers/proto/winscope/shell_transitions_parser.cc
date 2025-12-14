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

#include "src/trace_processor/importers/proto/winscope/shell_transitions_parser.h"

#include <optional>

#include "perfetto/base/status.h"
#include "perfetto/ext/base/base64.h"
#include "perfetto/ext/base/string_view.h"
#include "perfetto/protozero/field.h"
#include "protos/perfetto/trace/android/shell_transition.pbzero.h"
#include "src/trace_processor/importers/proto/args_parser.h"
#include "src/trace_processor/importers/proto/winscope/shell_transitions_tracker.h"
#include "src/trace_processor/importers/proto/winscope/winscope_context.h"
#include "src/trace_processor/storage/stats.h"
#include "src/trace_processor/storage/trace_storage.h"
#include "src/trace_processor/tables/winscope_tables_py.h"
#include "src/trace_processor/types/trace_processor_context.h"
#include "src/trace_processor/util/winscope_proto_mapping.h"

namespace perfetto {
namespace trace_processor {

ShellTransitionsParser::ShellTransitionsParser(
    winscope::WinscopeContext* context)
    : context_(context),
      args_parser_{*context->trace_processor_context_->descriptor_pool_} {}

void ShellTransitionsParser::ParseTransition(protozero::ConstBytes blob) {
  protos::pbzero::ShellTransition::Decoder transition(blob);

  auto* storage = context_->trace_processor_context_->storage.get();

  // Store the raw proto and its ID in a separate table to handle
  // transitions received over multiple packets for Winscope trace search.
  tables::WindowManagerShellTransitionProtosTable::Row row;
  row.transition_id = transition.id();
  row.base64_proto_id = storage->mutable_string_pool()
                            ->InternString(base::StringView(
                                base::Base64Encode(blob.data, blob.size)))
                            .raw_id();
  storage->mutable_window_manager_shell_transition_protos_table()->Insert(row);

  // Track transition args as the come in through different packets
  winscope::ShellTransitionsTracker& transition_tracker =
      context_->shell_transitions_tracker_;
  auto inserter = transition_tracker.AddArgsTo(transition.id());
  ArgsParser writer(/*timestamp=*/0, inserter, *storage);
  base::Status status = args_parser_.ParseMessage(
      blob,
      *util::winscope_proto_mapping::GetProtoName(
          tables::WindowManagerShellTransitionProtosTable::Name()),
      nullptr /* parse all fields */, writer);

  if (!status.ok()) {
    storage->IncrementStats(stats::winscope_shell_transitions_parse_errors);
  }

  if (transition.has_type()) {
    transition_tracker.SetTransitionType(transition.id(), transition.type());
  }

  if (transition.has_dispatch_time_ns()) {
    transition_tracker.SetDispatchTime(transition.id(),
                                       transition.dispatch_time_ns());
    transition_tracker.SetTimestamp(transition.id(),
                                    transition.dispatch_time_ns());
  }

  if (transition.has_send_time_ns()) {
    transition_tracker.SetSendTime(transition.id(), transition.send_time_ns());
    transition_tracker.SetTimestampIfEmpty(transition.id(),
                                           transition.send_time_ns());
  }

  if (transition.has_shell_abort_time_ns()) {
    transition_tracker.SetShellAbortTime(transition.id(),
                                         transition.shell_abort_time_ns());
  }

  if (transition.has_finish_time_ns()) {
    auto finish_time = transition.finish_time_ns();
    transition_tracker.SetFinishTime(transition.id(), finish_time);

    if (finish_time > 0) {
      transition_tracker.SetStatus(
          transition.id(),
          storage->mutable_string_pool()->InternString("played"));
    }
  }

  if (transition.has_handler()) {
    transition_tracker.SetHandler(transition.id(), transition.handler());
  }

  auto shell_aborted = transition.has_shell_abort_time_ns() &&
                       transition.shell_abort_time_ns() > 0;
  auto wm_aborted =
      transition.has_wm_abort_time_ns() && transition.wm_abort_time_ns() > 0;

  if (shell_aborted || wm_aborted) {
    transition_tracker.SetStatus(
        transition.id(),
        storage->mutable_string_pool()->InternString("aborted"));
  }

  auto merged =
      transition.has_merge_time_ns() && transition.merge_time_ns() > 0;
  if (merged) {
    transition_tracker.SetStatus(
        transition.id(),
        storage->mutable_string_pool()->InternString("merged"));
  }

  // set flags id and flags
  if (transition.has_flags()) {
    transition_tracker.SetFlags(transition.id(), transition.flags());
  }

  // update participants
  if (transition.has_targets()) {
    auto* participants_table =
        storage->mutable_window_manager_shell_transition_participants_table();
    for (auto it = transition.targets(); it; ++it) {
      tables::WindowManagerShellTransitionParticipantsTable::Row
          participant_row;
      participant_row.transition_id = transition.id();
      protos::pbzero::ShellTransition::Target::Decoder target(*it);
      if (target.has_layer_id()) {
        participant_row.layer_id = target.layer_id();
      }
      if (target.has_window_id()) {
        participant_row.window_id = target.window_id();
      }
      participants_table->Insert(participant_row);
    }
  }

  if (transition.has_start_transaction_id()) {
    transition_tracker.SetStartTransactionId(transition.id(),
                                             transition.start_transaction_id());
  }

  if (transition.has_finish_transaction_id()) {
    transition_tracker.SetFinishTransactionId(
        transition.id(), transition.finish_transaction_id());
  }
}

void ShellTransitionsParser::ParseHandlerMappings(protozero::ConstBytes blob) {
  auto* storage = context_->trace_processor_context_->storage.get();

  auto* shell_handlers_table =
      storage->mutable_window_manager_shell_transition_handlers_table();

  protos::pbzero::ShellHandlerMappings::Decoder handler_mappings(blob);
  for (auto it = handler_mappings.mapping(); it; ++it) {
    protos::pbzero::ShellHandlerMapping::Decoder mapping(it.field().as_bytes());

    tables::WindowManagerShellTransitionHandlersTable::Row row;
    row.handler_id = mapping.id();
    row.handler_name =
        storage->InternString(base::StringView(mapping.name().ToStdString()));
    row.base64_proto_id = storage->mutable_string_pool()
                              ->InternString(base::StringView(
                                  base::Base64Encode(blob.data, blob.size)))
                              .raw_id();
    shell_handlers_table->Insert(row);
  }
}

}  // namespace trace_processor
}  // namespace perfetto
