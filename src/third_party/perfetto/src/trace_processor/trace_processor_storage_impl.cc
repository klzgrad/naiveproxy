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

#include "src/trace_processor/trace_processor_storage_impl.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <utility>

#include "perfetto/base/status.h"
#include "perfetto/ext/base/status_macros.h"
#include "perfetto/ext/base/string_view.h"
#include "perfetto/ext/base/uuid.h"
#include "perfetto/trace_processor/basic_types.h"
#include "src/trace_processor/forwarding_trace_parser.h"
#include "src/trace_processor/importers/common/args_tracker.h"
#include "src/trace_processor/importers/common/clock_converter.h"  // IWYU pragma: keep
#include "src/trace_processor/importers/common/clock_tracker.h"
#include "src/trace_processor/importers/common/event_tracker.h"
#include "src/trace_processor/importers/common/metadata_tracker.h"
#include "src/trace_processor/importers/common/process_tracker.h"
#include "src/trace_processor/importers/common/slice_tracker.h"
#include "src/trace_processor/importers/common/stack_profile_tracker.h"
#include "src/trace_processor/importers/common/trace_file_tracker.h"
#include "src/trace_processor/importers/proto/default_modules.h"
#include "src/trace_processor/importers/proto/packet_analyzer.h"
#include "src/trace_processor/importers/proto/proto_importer_module.h"
#include "src/trace_processor/importers/proto/proto_trace_parser_impl.h"
#include "src/trace_processor/importers/proto/proto_trace_reader.h"
#include "src/trace_processor/sorter/trace_sorter.h"
#include "src/trace_processor/storage/metadata.h"
#include "src/trace_processor/storage/stats.h"
#include "src/trace_processor/storage/trace_storage.h"
#include "src/trace_processor/trace_reader_registry.h"
#include "src/trace_processor/types/variadic.h"
#include "src/trace_processor/util/descriptors.h"
#include "src/trace_processor/util/trace_type.h"

namespace perfetto::trace_processor {

TraceProcessorStorageImpl::TraceProcessorStorageImpl(const Config& cfg)
    : context_({cfg, std::make_shared<TraceStorage>(cfg)}) {
  context_.reader_registry->RegisterTraceReader<ProtoTraceReader>(
      kProtoTraceType);
  context_.reader_registry->RegisterTraceReader<ProtoTraceReader>(
      kSymbolsTraceType);
  context_.proto_trace_parser =
      std::make_unique<ProtoTraceParserImpl>(&context_);
  RegisterDefaultModules(&context_);
}

TraceProcessorStorageImpl::~TraceProcessorStorageImpl() {}

base::Status TraceProcessorStorageImpl::Parse(TraceBlobView blob) {
  if (blob.size() == 0)
    return base::OkStatus();
  if (unrecoverable_parse_error_)
    return base::ErrStatus(
        "Failed unrecoverably while parsing in a previous Parse call");
  if (eof_) {
    return base::ErrStatus("Parse() called after NotifyEndOfFile()");
  }

  if (!parser_) {
    auto parser = std::make_unique<ForwardingTraceParser>(
        &context_, context_.trace_file_tracker->AddFile());
    parser_ = parser.get();
    context_.chunk_readers.push_back(std::move(parser));
  }

  auto scoped_trace = context_.storage->TraceExecutionTimeIntoStats(
      stats::parse_trace_duration_ns);

  if (hash_input_size_remaining_ > 0 && !context_.uuid_found_in_trace) {
    const size_t hash_size = std::min(hash_input_size_remaining_, blob.size());
    hash_input_size_remaining_ -= hash_size;

    trace_hash_.Update(reinterpret_cast<const char*>(blob.data()), hash_size);
    base::Uuid uuid(static_cast<int64_t>(trace_hash_.digest()), 0);
    const StringId id_for_uuid =
        context_.storage->InternString(base::StringView(uuid.ToPrettyString()));
    context_.metadata_tracker->SetMetadata(metadata::trace_uuid,
                                           Variadic::String(id_for_uuid));
  }

  base::Status status = parser_->Parse(std::move(blob));
  unrecoverable_parse_error_ |= !status.ok();
  return status;
}

void TraceProcessorStorageImpl::Flush() {
  if (unrecoverable_parse_error_)
    return;

  if (context_.sorter)
    context_.sorter->ExtractEventsForced();
  context_.args_tracker->Flush();
}

base::Status TraceProcessorStorageImpl::NotifyEndOfFile() {
  if (!parser_) {
    return base::OkStatus();
  }
  if (unrecoverable_parse_error_) {
    return base::ErrStatus("Unrecoverable parsing error already occurred");
  }
  eof_ = true;
  Flush();
  RETURN_IF_ERROR(parser_->NotifyEndOfFile());
  // NotifyEndOfFile might have pushed packets to the sorter.
  Flush();
  for (std::unique_ptr<ProtoImporterModule>& module : context_.modules) {
    module->NotifyEndOfFile();
  }
  if (context_.content_analyzer) {
    PacketAnalyzer::Get(&context_)->NotifyEndOfFile();
  }

  context_.event_tracker->FlushPendingEvents();
  context_.slice_tracker->FlushPendingSlices();
  context_.args_tracker->Flush();
  context_.process_tracker->NotifyEndOfFile();
  return base::OkStatus();
}

void TraceProcessorStorageImpl::DestroyContext() {
  TraceProcessorContext context;
  context.storage = std::move(context_.storage);

  // TODO(b/309623584): Decouple from storage and remove from here. This
  // function should only move storage and delete everything else.
  context.heap_graph_tracker = std::move(context_.heap_graph_tracker);
  context.clock_converter = std::move(context_.clock_converter);
  // "to_ftrace" textual converter of the "raw" table requires remembering the
  // kernel version (inside system_info_tracker) to know how to textualise
  // sched_switch.prev_state bitflags.
  context.system_info_tracker = std::move(context_.system_info_tracker);

#if PERFETTO_BUILDFLAG(PERFETTO_ENABLE_WINSCOPE)
  // "__intrinsic_winscope_proto_to_args_with_defaults" requires proto
  // descriptors.
  context.descriptor_pool_ = std::move(context_.descriptor_pool_);
#endif

  context_ = std::move(context);

  // This is now a dangling pointer, reset it.
  parser_ = nullptr;

  // TODO(chinglinyu): also need to destroy secondary contextes.
}

}  // namespace perfetto::trace_processor
