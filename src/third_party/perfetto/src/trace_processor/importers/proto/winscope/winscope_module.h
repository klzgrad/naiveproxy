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

#ifndef SRC_TRACE_PROCESSOR_IMPORTERS_PROTO_WINSCOPE_WINSCOPE_MODULE_H_
#define SRC_TRACE_PROCESSOR_IMPORTERS_PROTO_WINSCOPE_WINSCOPE_MODULE_H_

#include <cstdint>
#include "perfetto/protozero/field.h"
#include "perfetto/trace_processor/ref_counted.h"
#include "protos/perfetto/trace/trace_packet.pbzero.h"
#include "src/trace_processor/importers/common/parser_types.h"
#include "src/trace_processor/importers/proto/packet_sequence_state_generation.h"
#include "src/trace_processor/importers/proto/proto_importer_module.h"
#include "src/trace_processor/importers/proto/winscope/android_input_event_parser.h"
#include "src/trace_processor/importers/proto/winscope/protolog_parser.h"
#include "src/trace_processor/importers/proto/winscope/shell_transitions_parser.h"
#include "src/trace_processor/importers/proto/winscope/surfaceflinger_layers_parser.h"
#include "src/trace_processor/importers/proto/winscope/surfaceflinger_transactions_parser.h"
#include "src/trace_processor/importers/proto/winscope/viewcapture_parser.h"
#include "src/trace_processor/importers/proto/winscope/windowmanager_parser.h"
#include "src/trace_processor/importers/proto/winscope/winscope_context.h"
#include "src/trace_processor/util/proto_to_args_parser.h"

namespace perfetto::trace_processor {

class WinscopeModule : public ProtoImporterModule {
 public:
  explicit WinscopeModule(ProtoImporterModuleContext* module_context,
                          TraceProcessorContext* context);

  ModuleResult TokenizePacket(
      const protos::pbzero::TracePacket::Decoder& decoder,
      TraceBlobView* packet,
      int64_t packet_timestamp,
      RefPtr<PacketSequenceStateGeneration> state,
      uint32_t field_id) override;

  void ParseTracePacketData(const protos::pbzero::TracePacket::Decoder&,
                            int64_t ts,
                            const TracePacketData&,
                            uint32_t field_id) override;

  void NotifyEndOfFile() override;

 private:
  void ParseWinscopeExtensionsData(protozero::ConstBytes blob,
                                   int64_t timestamp,
                                   const TracePacketData&);
  void ParseInputMethodClientsData(int64_t timestamp,
                                   protozero::ConstBytes blob);
  void ParseInputMethodManagerServiceData(int64_t timestamp,
                                          protozero::ConstBytes blob);
  void ParseInputMethodServiceData(int64_t timestamp,
                                   protozero::ConstBytes blob);

  winscope::WinscopeContext context_;
  util::ProtoToArgsParser args_parser_;

  winscope::SurfaceFlingerLayersParser surfaceflinger_layers_parser_;
  SurfaceFlingerTransactionsParser surfaceflinger_transactions_parser_;
  ShellTransitionsParser shell_transitions_parser_;
  ProtoLogParser protolog_parser_;
  AndroidInputEventParser android_input_event_parser_;
  winscope::ViewCaptureParser viewcapture_parser_;
  winscope::WindowManagerParser windowmanager_parser_;
};

}  // namespace perfetto::trace_processor

#endif  // SRC_TRACE_PROCESSOR_IMPORTERS_PROTO_WINSCOPE_WINSCOPE_MODULE_H_
