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

#ifndef SRC_TRACE_PROCESSOR_IMPORTERS_PROTO_HEAP_GRAPH_MODULE_H_
#define SRC_TRACE_PROCESSOR_IMPORTERS_PROTO_HEAP_GRAPH_MODULE_H_

#include <cstdint>

#include "perfetto/protozero/field.h"
#include "src/trace_processor/importers/common/parser_types.h"
#include "src/trace_processor/importers/proto/heap_graph_tracker.h"
#include "src/trace_processor/importers/proto/proto_importer_module.h"

#include "protos/perfetto/trace/trace_packet.pbzero.h"

namespace perfetto::trace_processor {

class HeapGraphModule : public ProtoImporterModule {
 public:
  explicit HeapGraphModule(ProtoImporterModuleContext* module_context,
                           TraceProcessorContext* context);

  void ParseTracePacketData(const protos::pbzero::TracePacket_Decoder& decoder,
                            int64_t ts,
                            const TracePacketData&,
                            uint32_t field_id) override;

  void NotifyEndOfFile() override;

 private:
  void ParseHeapGraph(uint32_t seq_id, int64_t ts, protozero::ConstBytes);

  TraceProcessorContext* context_;
};

}  // namespace perfetto::trace_processor

#endif  // SRC_TRACE_PROCESSOR_IMPORTERS_PROTO_HEAP_GRAPH_MODULE_H_
