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

#ifndef SRC_TRACE_PROCESSOR_PLUGINS_WINSCOPE_IMPORTER_WINSCOPE_MODULE_H_
#define SRC_TRACE_PROCESSOR_PLUGINS_WINSCOPE_IMPORTER_WINSCOPE_MODULE_H_

#include <cstdint>
#include "perfetto/protozero/field.h"
#include "perfetto/trace_processor/ref_counted.h"
#include "protos/perfetto/trace/trace_packet.pbzero.h"
#include "src/trace_processor/importers/common/parser_types.h"
#include "src/trace_processor/importers/proto/packet_sequence_state_generation.h"
#include "src/trace_processor/importers/proto/proto_importer_module.h"
#include "src/trace_processor/plugins/winscope_importer/android_input_event_parser.h"
#include "src/trace_processor/plugins/winscope_importer/protolog_parser.h"
#include "src/trace_processor/plugins/winscope_importer/shell_transitions_parser.h"
#include "src/trace_processor/plugins/winscope_importer/surfaceflinger_layers_parser.h"
#include "src/trace_processor/plugins/winscope_importer/surfaceflinger_transactions_parser.h"
#include "src/trace_processor/plugins/winscope_importer/viewcapture_parser.h"
#include "src/trace_processor/plugins/winscope_importer/windowmanager_parser.h"
#include "src/trace_processor/plugins/winscope_importer/winscope_context.h"
#include "src/trace_processor/util/proto_to_args_parser.h"

namespace perfetto::trace_processor {

class WinscopeModule : public ProtoImporterModule {
 public:
  explicit WinscopeModule(ProtoImporterModuleContext* module_context,
                          TraceProcessorContext* context);

  ModuleResult TokenizePacket(const TokenizePacketArgs& args) override;

  void ParseField(const ParseFieldArgs& args) override;

  void OnEventsFullyExtracted() override;

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

#endif  // SRC_TRACE_PROCESSOR_PLUGINS_WINSCOPE_IMPORTER_WINSCOPE_MODULE_H_
