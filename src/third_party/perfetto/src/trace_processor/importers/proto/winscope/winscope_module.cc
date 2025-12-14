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

#include "src/trace_processor/importers/proto/winscope/winscope_module.h"

#include <cstdint>
#include <optional>

#include "perfetto/base/status.h"
#include "perfetto/ext/base/base64.h"
#include "perfetto/ext/base/string_view.h"
#include "perfetto/protozero/field.h"
#include "perfetto/trace_processor/ref_counted.h"
#include "protos/perfetto/trace/android/winscope_extensions.pbzero.h"
#include "protos/perfetto/trace/android/winscope_extensions_impl.pbzero.h"
#include "protos/perfetto/trace/trace_packet.pbzero.h"
#include "src/trace_processor/importers/common/args_tracker.h"
#include "src/trace_processor/importers/common/parser_types.h"
#include "src/trace_processor/importers/proto/args_parser.h"
#include "src/trace_processor/importers/proto/packet_sequence_state_generation.h"
#include "src/trace_processor/importers/proto/proto_importer_module.h"
#include "src/trace_processor/importers/proto/winscope/shell_transitions_tracker.h"
#include "src/trace_processor/importers/proto/winscope/winscope.descriptor.h"
#include "src/trace_processor/storage/stats.h"
#include "src/trace_processor/tables/winscope_tables_py.h"
#include "src/trace_processor/util/winscope_proto_mapping.h"

namespace perfetto::trace_processor {

using perfetto::protos::pbzero::TracePacket;
using perfetto::protos::pbzero::WinscopeExtensionsImpl;

WinscopeModule::WinscopeModule(ProtoImporterModuleContext* module_context,
                               TraceProcessorContext* context)
    : ProtoImporterModule(module_context),
      context_{context},
      args_parser_{*context->descriptor_pool_},
      surfaceflinger_layers_parser_(&context_),
      surfaceflinger_transactions_parser_(context),
      shell_transitions_parser_(&context_),
      protolog_parser_(&context_),
      android_input_event_parser_(context),
      viewcapture_parser_(&context_),
      windowmanager_parser_(&context_) {
  context->descriptor_pool_->AddFromFileDescriptorSet(
      kWinscopeDescriptor.data(), kWinscopeDescriptor.size());
  RegisterForField(TracePacket::kSurfaceflingerLayersSnapshotFieldNumber);
  RegisterForField(TracePacket::kSurfaceflingerTransactionsFieldNumber);
  RegisterForField(TracePacket::kShellTransitionFieldNumber);
  RegisterForField(TracePacket::kShellHandlerMappingsFieldNumber);
  RegisterForField(TracePacket::kProtologMessageFieldNumber);
  RegisterForField(TracePacket::kProtologViewerConfigFieldNumber);
  RegisterForField(TracePacket::kWinscopeExtensionsFieldNumber);
}

ModuleResult WinscopeModule::TokenizePacket(
    const protos::pbzero::TracePacket::Decoder& decoder,
    TraceBlobView* /*packet*/,
    int64_t /*packet_timestamp*/,
    RefPtr<PacketSequenceStateGeneration> /*state*/,
    uint32_t field_id) {
  switch (field_id) {
    case TracePacket::kProtologViewerConfigFieldNumber:
      protolog_parser_.ParseAndAddViewerConfigToMessageDecoder(
          decoder.protolog_viewer_config());
      return ModuleResult::Handled();
  }

  return ModuleResult::Ignored();
}

void WinscopeModule::ParseTracePacketData(const TracePacket::Decoder& decoder,
                                          int64_t timestamp,
                                          const TracePacketData& data,
                                          uint32_t field_id) {
  std::optional<uint32_t> sequence_id;
  if (decoder.has_trusted_packet_sequence_id()) {
    sequence_id = decoder.trusted_packet_sequence_id();
  }
  switch (field_id) {
    case TracePacket::kSurfaceflingerLayersSnapshotFieldNumber:
      surfaceflinger_layers_parser_.Parse(
          timestamp, decoder.surfaceflinger_layers_snapshot(), sequence_id);
      return;
    case TracePacket::kSurfaceflingerTransactionsFieldNumber:
      surfaceflinger_transactions_parser_.Parse(
          timestamp, decoder.surfaceflinger_transactions());
      return;
    case TracePacket::kShellTransitionFieldNumber:
      shell_transitions_parser_.ParseTransition(decoder.shell_transition());
      return;
    case TracePacket::kShellHandlerMappingsFieldNumber:
      shell_transitions_parser_.ParseHandlerMappings(
          decoder.shell_handler_mappings());
      return;
    case TracePacket::kProtologMessageFieldNumber:
      protolog_parser_.ParseProtoLogMessage(
          data.sequence_state.get(), decoder.protolog_message(), timestamp);
      return;
    case TracePacket::kWinscopeExtensionsFieldNumber:
      ParseWinscopeExtensionsData(decoder.winscope_extensions(), timestamp,
                                  data);
      return;
  }
}

void WinscopeModule::ParseWinscopeExtensionsData(protozero::ConstBytes blob,
                                                 int64_t timestamp,
                                                 const TracePacketData& data) {
  WinscopeExtensionsImpl::Decoder decoder(blob.data, blob.size);

  if (auto field =
          decoder.Get(WinscopeExtensionsImpl::kInputmethodClientsFieldNumber);
      field.valid()) {
    ParseInputMethodClientsData(timestamp, field.as_bytes());
  } else if (field = decoder.Get(
                 WinscopeExtensionsImpl::kInputmethodManagerServiceFieldNumber);
             field.valid()) {
    ParseInputMethodManagerServiceData(timestamp, field.as_bytes());
  } else if (field = decoder.Get(
                 WinscopeExtensionsImpl::kInputmethodServiceFieldNumber);
             field.valid()) {
    ParseInputMethodServiceData(timestamp, field.as_bytes());
  } else if (field =
                 decoder.Get(WinscopeExtensionsImpl::kViewcaptureFieldNumber);
             field.valid()) {
    viewcapture_parser_.Parse(timestamp, field.as_bytes(),
                              data.sequence_state.get());
  } else if (field = decoder.Get(
                 WinscopeExtensionsImpl::kAndroidInputEventFieldNumber);
             field.valid()) {
    android_input_event_parser_.ParseAndroidInputEvent(timestamp,
                                                       field.as_bytes());
  } else if (field =
                 decoder.Get(WinscopeExtensionsImpl::kWindowmanagerFieldNumber);
             field.valid()) {
    windowmanager_parser_.Parse(timestamp, field.as_bytes());
  }
}

void WinscopeModule::ParseInputMethodClientsData(int64_t timestamp,
                                                 protozero::ConstBytes blob) {
  auto* trace_processor_context = context_.trace_processor_context_;
  tables::InputMethodClientsTable::Row row;
  row.ts = timestamp;
  row.base64_proto_id = trace_processor_context->storage->mutable_string_pool()
                            ->InternString(base::StringView(
                                base::Base64Encode(blob.data, blob.size)))
                            .raw_id();
  auto rowId =
      trace_processor_context->storage->mutable_inputmethod_clients_table()
          ->Insert(row)
          .id;

  ArgsTracker tracker(trace_processor_context);
  auto inserter = tracker.AddArgsTo(rowId);
  ArgsParser writer(timestamp, inserter, *trace_processor_context->storage);
  base::Status status =
      args_parser_.ParseMessage(blob,
                                *util::winscope_proto_mapping::GetProtoName(
                                    tables::InputMethodClientsTable::Name()),
                                nullptr /* parse all fields */, writer);
  if (!status.ok()) {
    trace_processor_context->storage->IncrementStats(
        stats::winscope_inputmethod_clients_parse_errors);
  }
}

void WinscopeModule::ParseInputMethodManagerServiceData(
    int64_t timestamp,
    protozero::ConstBytes blob) {
  auto* trace_processor_context = context_.trace_processor_context_;
  tables::InputMethodManagerServiceTable::Row row;
  row.ts = timestamp;
  row.base64_proto_id = trace_processor_context->storage->mutable_string_pool()
                            ->InternString(base::StringView(
                                base::Base64Encode(blob.data, blob.size)))
                            .raw_id();
  auto rowId = trace_processor_context->storage
                   ->mutable_inputmethod_manager_service_table()
                   ->Insert(row)
                   .id;

  ArgsTracker tracker(trace_processor_context);
  auto inserter = tracker.AddArgsTo(rowId);
  ArgsParser writer(timestamp, inserter, *trace_processor_context->storage);
  base::Status status = args_parser_.ParseMessage(
      blob,
      *util::winscope_proto_mapping::GetProtoName(
          tables::InputMethodManagerServiceTable::Name()),
      nullptr /* parse all fields */, writer);
  if (!status.ok()) {
    trace_processor_context->storage->IncrementStats(
        stats::winscope_inputmethod_manager_service_parse_errors);
  }
}

void WinscopeModule::ParseInputMethodServiceData(int64_t timestamp,
                                                 protozero::ConstBytes blob) {
  auto* trace_processor_context = context_.trace_processor_context_;
  tables::InputMethodServiceTable::Row row;
  row.ts = timestamp;
  row.base64_proto_id = trace_processor_context->storage->mutable_string_pool()
                            ->InternString(base::StringView(
                                base::Base64Encode(blob.data, blob.size)))
                            .raw_id();
  auto rowId =
      trace_processor_context->storage->mutable_inputmethod_service_table()
          ->Insert(row)
          .id;

  ArgsTracker tracker(trace_processor_context);
  auto inserter = tracker.AddArgsTo(rowId);
  ArgsParser writer(timestamp, inserter, *trace_processor_context->storage);
  base::Status status =
      args_parser_.ParseMessage(blob,
                                *util::winscope_proto_mapping::GetProtoName(
                                    tables::InputMethodServiceTable::Name()),
                                nullptr /* parse all fields */, writer);
  if (!status.ok()) {
    trace_processor_context->storage->IncrementStats(
        stats::winscope_inputmethod_service_parse_errors);
  }
}

void WinscopeModule::NotifyEndOfFile() {
  context_.shell_transitions_tracker_.Flush();
}

}  // namespace perfetto::trace_processor
