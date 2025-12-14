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

#ifndef SRC_TRACE_PROCESSOR_IMPORTERS_PROTO_DEOBFUSCATION_MODULE_H_
#define SRC_TRACE_PROCESSOR_IMPORTERS_PROTO_DEOBFUSCATION_MODULE_H_

#include "perfetto/protozero/field.h"
#include "protos/perfetto/trace/profiling/deobfuscation.pbzero.h"
#include "protos/perfetto/trace/trace_packet.pbzero.h"
#include "src/trace_processor/importers/common/parser_types.h"
#include "src/trace_processor/importers/proto/proto_importer_module.h"

namespace perfetto::trace_processor {

class DeobfuscationModule : public ProtoImporterModule {
 public:
  explicit DeobfuscationModule(ProtoImporterModuleContext* module_context,
                               TraceProcessorContext* context);
  ~DeobfuscationModule() override;

  // TODO (ddiproietto): Is it better to use TokenizePacket instead?
  void ParseTracePacketData(const protos::pbzero::TracePacket::Decoder& decoder,
                            int64_t ts,
                            const TracePacketData& data,
                            uint32_t field_id) override;

  void NotifyEndOfFile() override;

 private:
  void StoreDeobfuscationMapping(protozero::ConstBytes);
  void BuildMappingTableIncremental(
      const protos::pbzero::DeobfuscationMapping::Decoder& mapping);

  TraceProcessorContext* context_;
};

}  // namespace perfetto::trace_processor

#endif  // SRC_TRACE_PROCESSOR_IMPORTERS_PROTO_DEOBFUSCATION_MODULE_H_
