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

#include "src/trace_processor/importers/proto/winscope/protolog_parser.h"

#include <cinttypes>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "perfetto/ext/base/flat_hash_map.h"
#include "perfetto/ext/base/string_utils.h"
#include "perfetto/ext/base/string_view.h"
#include "perfetto/protozero/field.h"
#include "protos/perfetto/trace/android/protolog.pbzero.h"
#include "protos/perfetto/trace/interned_data/interned_data.pbzero.h"
#include "protos/perfetto/trace/profiling/profile_common.pbzero.h"
#include "protos/perfetto/trace/profiling/profile_packet.pbzero.h"
#include "src/trace_processor/containers/string_pool.h"
#include "src/trace_processor/importers/proto/packet_sequence_state_generation.h"
#include "src/trace_processor/importers/proto/winscope/protolog_message_decoder.h"
#include "src/trace_processor/storage/stats.h"
#include "src/trace_processor/storage/trace_storage.h"
#include "src/trace_processor/tables/winscope_tables_py.h"
#include "src/trace_processor/types/trace_processor_context.h"

namespace perfetto::trace_processor {

ProtoLogParser::ProtoLogParser(TraceProcessorContext* context)
    : context_(context),
      args_parser_{*context_->descriptor_pool_},
      log_level_debug_string_id_(context->storage->InternString("DEBUG")),
      log_level_verbose_string_id_(context->storage->InternString("VERBOSE")),
      log_level_info_string_id_(context->storage->InternString("INFO")),
      log_level_warn_string_id_(context->storage->InternString("WARN")),
      log_level_error_string_id_(context->storage->InternString("ERROR")),
      log_level_wtf_string_id_(context->storage->InternString("WTF")),
      log_level_unknown_string_id_(context_->storage->InternString("UNKNOWN")) {
}

void ProtoLogParser::ParseProtoLogMessage(
    PacketSequenceStateGeneration* sequence_state,
    protozero::ConstBytes blob,
    int64_t timestamp) {
  protos::pbzero::ProtoLogMessage::Decoder protolog_message(blob);

  std::vector<int64_t> sint64_params;
  for (auto it = protolog_message.sint64_params(); it; ++it) {
    sint64_params.emplace_back(it->as_sint64());
  }

  std::vector<double> double_params;
  for (auto it = protolog_message.double_params(); it; ++it) {
    double_params.emplace_back(*it);
  }

  std::vector<bool> boolean_params;
  for (auto it = protolog_message.boolean_params(); it; ++it) {
    boolean_params.emplace_back(*it);
  }

  std::vector<std::string> string_params;
  if (protolog_message.has_str_param_iids()) {
    for (auto it = protolog_message.str_param_iids(); it; ++it) {
      auto* decoder = sequence_state->LookupInternedMessage<
          protos::pbzero::InternedData::kProtologStringArgsFieldNumber,
          protos::pbzero::InternedString>(it.field().as_uint32());
      if (!decoder) {
        // This shouldn't happen since we already checked the incremental
        // state is valid.
        string_params.emplace_back("<ERROR>");
        context_->storage->IncrementStats(
            stats::winscope_protolog_missing_interned_arg_parse_errors);
        continue;
      }
      string_params.emplace_back(decoder->str().ToStdString());
    }
  }

  std::optional<StringId> stacktrace = std::nullopt;
  if (protolog_message.has_stacktrace_iid()) {
    auto* stacktrace_decoder = sequence_state->LookupInternedMessage<
        protos::pbzero::InternedData::kProtologStacktraceFieldNumber,
        protos::pbzero::InternedString>(protolog_message.stacktrace_iid());

    if (!stacktrace_decoder) {
      // This shouldn't happen since we already checked the incremental
      // state is valid.
      string_params.emplace_back("<ERROR>");
      context_->storage->IncrementStats(
          stats::winscope_protolog_missing_interned_stacktrace_parse_errors);
    } else {
      stacktrace = context_->storage->InternString(
          base::StringView(stacktrace_decoder->str().ToStdString()));
    }
  }

  auto* protolog_table = context_->storage->mutable_protolog_table();

  tables::ProtoLogTable::Row row;
  row.ts = timestamp;
  auto row_id = protolog_table->Insert(row).id;

  auto* protolog_message_decoder =
      ProtoLogMessageDecoder::GetOrCreate(context_);

  auto decoded_message_opt = protolog_message_decoder->Decode(
      protolog_message.message_id(), sint64_params, double_params,
      boolean_params, string_params);
  if (decoded_message_opt.has_value()) {
    auto decoded_message = decoded_message_opt.value();
    std::optional<std::string> location = decoded_message.location;
    PopulateReservedRowWithMessage(
        row_id, decoded_message.log_level, decoded_message.group_tag,
        decoded_message.message, stacktrace, location);
  } else {
    // Failed to fully decode the message.
    // This shouldn't happen since we should have processed all viewer config
    // messages in the tokenization state, and process the protolog messages
    // only in the parsing state.
    context_->storage->IncrementStats(
        stats::winscope_protolog_message_decoding_failed);
  }
}

void ProtoLogParser::ParseAndAddViewerConfigToMessageDecoder(
    protozero::ConstBytes blob) {
  protos::pbzero::ProtoLogViewerConfig::Decoder protolog_viewer_config(blob);

  auto* protolog_message_decoder =
      ProtoLogMessageDecoder::GetOrCreate(context_);

  for (auto it = protolog_viewer_config.groups(); it; ++it) {
    protos::pbzero::ProtoLogViewerConfig::Group::Decoder group(*it);
    protolog_message_decoder->TrackGroup(group.id(), group.tag().ToStdString());
  }

  for (auto it = protolog_viewer_config.messages(); it; ++it) {
    protos::pbzero::ProtoLogViewerConfig::MessageData::Decoder message_data(
        *it);

    std::optional<std::string> location = std::nullopt;
    if (message_data.has_location()) {
      location = message_data.location().ToStdString();
    }

    protolog_message_decoder->TrackMessage(
        message_data.message_id(),
        static_cast<ProtoLogLevel>(message_data.level()),
        message_data.group_id(), message_data.message().ToStdString(),
        location);
  }
}

void ProtoLogParser::PopulateReservedRowWithMessage(
    tables::ProtoLogTable::Id table_row_id,
    ProtoLogLevel log_level,
    std::string& group_tag,
    std::string& message,
    std::optional<StringId> stacktrace,
    std::optional<std::string>& location) {
  auto* protolog_table = context_->storage->mutable_protolog_table();
  auto row = protolog_table->FindById(table_row_id).value();

  StringPool::Id level;
  switch (log_level) {
    case ProtoLogLevel::DEBUG:
      level = log_level_debug_string_id_;
      break;
    case ProtoLogLevel::VERBOSE:
      level = log_level_verbose_string_id_;
      break;
    case ProtoLogLevel::INFO:
      level = log_level_info_string_id_;
      break;
    case ProtoLogLevel::WARN:
      level = log_level_warn_string_id_;
      break;
    case ProtoLogLevel::ERROR:
      level = log_level_error_string_id_;
      break;
    case ProtoLogLevel::WTF:
      level = log_level_wtf_string_id_;
      break;
  }
  row.set_level(level);

  auto tag = context_->storage->InternString(base::StringView(group_tag));
  row.set_tag(tag);

  auto message_string_id =
      context_->storage->InternString(base::StringView(message));
  row.set_message(message_string_id);

  if (stacktrace.has_value()) {
    row.set_stacktrace(stacktrace.value());
  }

  if (location.has_value()) {
    auto location_string_id =
        context_->storage->InternString(base::StringView(location.value()));
    row.set_location(location_string_id);
  }
}

}  // namespace perfetto::trace_processor
