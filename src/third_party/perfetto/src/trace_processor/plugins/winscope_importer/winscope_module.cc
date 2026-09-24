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

#include "src/trace_processor/plugins/winscope_importer/winscope_module.h"

#include <cstdint>
#include <optional>

#include "perfetto/base/status.h"
#include "perfetto/ext/base/base64.h"
#include "perfetto/ext/base/string_view.h"
#include "perfetto/protozero/field.h"
#include "perfetto/protozero/proto_decoder.h"
#include "perfetto/trace_processor/ref_counted.h"
#include "protos/perfetto/trace/trace_packet.pbzero.h"
#include "protos/third_party/android/frameworks/base/proto/tracing/winscope/frameworks_base_winscope.pbzero.h"
#include "protos/third_party/android/frameworks/native/tracing/winscope/frameworks_native_winscope.pbzero.h"
#include "src/trace_processor/importers/common/args_tracker.h"
#include "src/trace_processor/importers/common/parser_types.h"
#include "src/trace_processor/importers/common/stats_tracker.h"
#include "src/trace_processor/importers/proto/args_parser.h"
#include "src/trace_processor/importers/proto/packet_sequence_state_generation.h"
#include "src/trace_processor/importers/proto/proto_importer_module.h"
#include "src/trace_processor/plugins/winscope_importer/shell_transitions_tracker.h"
#include "src/trace_processor/plugins/winscope_importer/winscope_proto_mapping.h"
#include "src/trace_processor/storage/stats.h"
#include "src/trace_processor/tables/winscope_tables_py.h"

namespace perfetto::trace_processor {

using com::android::internal::pbzero::FrameworksBaseWinscopeExtensions;
using com::android::internal::pbzero::FrameworksBaseWinscopeTracePacket;
using com::android::internal::pbzero::FrameworksNativeWinscopeExtensions;
using com::android::internal::pbzero::FrameworksNativeWinscopeTracePacket;
using com::android::internal::pbzero::WinscopeExtensions;
using perfetto::protos::pbzero::TracePacket;

WinscopeModule::WinscopeModule(ProtoImporterModuleContext* module_context,
                               TraceProcessorContext* context)
    : ProtoImporterModule(module_context),
      context_{context},
      args_parser_{*context->descriptor_pool_,
                   *context->storage->mutable_string_pool()},
      surfaceflinger_layers_parser_(&context_),
      surfaceflinger_transactions_parser_(context),
      shell_transitions_parser_(&context_),
      protolog_parser_(&context_),
      android_input_event_parser_(context),
      viewcapture_parser_(&context_),
      windowmanager_parser_(&context_) {
  RegisterForField(FrameworksNativeWinscopeTracePacket::
                       kSurfaceflingerLayersSnapshotFieldNumber);
  RegisterForField(FrameworksNativeWinscopeTracePacket::
                       kSurfaceflingerTransactionsFieldNumber);
  RegisterForField(
      FrameworksBaseWinscopeTracePacket::kShellTransitionFieldNumber);
  RegisterForField(
      FrameworksBaseWinscopeTracePacket::kShellHandlerMappingsFieldNumber);
  RegisterForField(
      FrameworksNativeWinscopeTracePacket::kProtologMessageFieldNumber);
  RegisterForField(
      FrameworksNativeWinscopeTracePacket::kProtologViewerConfigFieldNumber);
  RegisterForField(
      FrameworksNativeWinscopeTracePacket::kWinscopeExtensionsFieldNumber);
}

ModuleResult WinscopeModule::TokenizePacket(const TokenizePacketArgs& args) {
  switch (args.field.id()) {
    case FrameworksNativeWinscopeTracePacket::kProtologViewerConfigFieldNumber:
      protolog_parser_.ParseAndAddViewerConfigToMessageDecoder(
          args.field.Cast<
              FrameworksNativeWinscopeTracePacket::kProtologViewerConfig>());
      return ModuleResult::Handled();
  }

  return ModuleResult::Ignored();
}

void WinscopeModule::ParseField(const ParseFieldArgs& args) {
  std::optional<uint32_t> sequence_id;
  if (args.decoder.has_trusted_packet_sequence_id()) {
    sequence_id = args.decoder.trusted_packet_sequence_id();
  }
  switch (args.field.id()) {
    case FrameworksNativeWinscopeTracePacket::
        kSurfaceflingerLayersSnapshotFieldNumber:
      surfaceflinger_layers_parser_.Parse(
          args.ts,
          args.field.Cast<FrameworksNativeWinscopeTracePacket::
                              kSurfaceflingerLayersSnapshot>(),
          sequence_id);
      return;
    case FrameworksNativeWinscopeTracePacket::
        kSurfaceflingerTransactionsFieldNumber:
      surfaceflinger_transactions_parser_.Parse(
          args.ts, args.field.Cast<FrameworksNativeWinscopeTracePacket::
                                       kSurfaceflingerTransactions>());
      return;
    case FrameworksBaseWinscopeTracePacket::kShellTransitionFieldNumber:
      shell_transitions_parser_.ParseTransition(
          args.field
              .Cast<FrameworksBaseWinscopeTracePacket::kShellTransition>());
      return;
    case FrameworksBaseWinscopeTracePacket::kShellHandlerMappingsFieldNumber:
      shell_transitions_parser_.ParseHandlerMappings(
          args.field.Cast<
              FrameworksBaseWinscopeTracePacket::kShellHandlerMappings>());
      return;
    case FrameworksNativeWinscopeTracePacket::kProtologMessageFieldNumber:
      protolog_parser_.ParseProtoLogMessage(
          args.data.sequence_state.get(),
          args.field
              .Cast<FrameworksNativeWinscopeTracePacket::kProtologMessage>(),
          args.ts);
      return;
    case FrameworksNativeWinscopeTracePacket::kWinscopeExtensionsFieldNumber:
      ParseWinscopeExtensionsData(
          args.field
              .Cast<FrameworksNativeWinscopeTracePacket::kWinscopeExtensions>(),
          args.ts, args.data);
      return;
  }
}

void WinscopeModule::ParseWinscopeExtensionsData(protozero::ConstBytes blob,
                                                 int64_t timestamp,
                                                 const TracePacketData& data) {
  // WinscopeExtensions is purely a carrier of extension fields: walk them
  // all in wire order and dispatch on the field id.
  protozero::ProtoDecoder decoder(blob);
  for (protozero::Field f = decoder.ReadField(); f.valid();
       f = decoder.ReadField()) {
    TypedProtoField field(f);
    switch (field.id()) {
      case FrameworksBaseWinscopeExtensions::kInputmethodClientsFieldNumber:
        ParseInputMethodClientsData(
            timestamp,
            field
                .Cast<FrameworksBaseWinscopeExtensions::kInputmethodClients>());
        return;
      case FrameworksBaseWinscopeExtensions::
          kInputmethodManagerServiceFieldNumber:
        ParseInputMethodManagerServiceData(
            timestamp, field.Cast<FrameworksBaseWinscopeExtensions::
                                      kInputmethodManagerService>());
        return;
      case FrameworksBaseWinscopeExtensions::kInputmethodServiceFieldNumber:
        ParseInputMethodServiceData(
            timestamp,
            field
                .Cast<FrameworksBaseWinscopeExtensions::kInputmethodService>());
        return;
      case FrameworksBaseWinscopeExtensions::kViewcaptureFieldNumber:
        viewcapture_parser_.Parse(
            timestamp,
            field.Cast<FrameworksBaseWinscopeExtensions::kViewcapture>(),
            data.sequence_state.get());
        return;
      case FrameworksNativeWinscopeExtensions::kAndroidInputEventFieldNumber:
        android_input_event_parser_.ParseAndroidInputEvent(
            timestamp,
            field.Cast<
                FrameworksNativeWinscopeExtensions::kAndroidInputEvent>());
        return;
      case FrameworksBaseWinscopeExtensions::kWindowmanagerFieldNumber:
        windowmanager_parser_.Parse(
            timestamp,
            field.Cast<FrameworksBaseWinscopeExtensions::kWindowmanager>());
        return;
    }
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
  ArgsParser writer(timestamp, inserter, *trace_processor_context->storage,
                    *trace_processor_context->process_tracker);
  base::Status status =
      args_parser_.ParseMessage(blob,
                                *util::winscope_proto_mapping::GetProtoName(
                                    tables::InputMethodClientsTable::Name()),
                                nullptr /* parse all fields */, writer);
  if (!status.ok()) {
    trace_processor_context->stats_tracker->IncrementStats(
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
  ArgsParser writer(timestamp, inserter, *trace_processor_context->storage,
                    *trace_processor_context->process_tracker);
  base::Status status = args_parser_.ParseMessage(
      blob,
      *util::winscope_proto_mapping::GetProtoName(
          tables::InputMethodManagerServiceTable::Name()),
      nullptr /* parse all fields */, writer);
  if (!status.ok()) {
    trace_processor_context->stats_tracker->IncrementStats(
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
  ArgsParser writer(timestamp, inserter, *trace_processor_context->storage,
                    *trace_processor_context->process_tracker);
  base::Status status =
      args_parser_.ParseMessage(blob,
                                *util::winscope_proto_mapping::GetProtoName(
                                    tables::InputMethodServiceTable::Name()),
                                nullptr /* parse all fields */, writer);
  if (!status.ok()) {
    trace_processor_context->stats_tracker->IncrementStats(
        stats::winscope_inputmethod_service_parse_errors);
  }
}

void WinscopeModule::OnEventsFullyExtracted() {
  context_.shell_transitions_tracker_.Flush();
}

}  // namespace perfetto::trace_processor
