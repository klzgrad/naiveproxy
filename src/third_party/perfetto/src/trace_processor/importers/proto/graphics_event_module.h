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

#ifndef SRC_TRACE_PROCESSOR_IMPORTERS_PROTO_GRAPHICS_EVENT_MODULE_H_
#define SRC_TRACE_PROCESSOR_IMPORTERS_PROTO_GRAPHICS_EVENT_MODULE_H_

#include <cstdint>

#include "src/trace_processor/importers/common/parser_types.h"
#include "src/trace_processor/importers/proto/frame_timeline_event_parser.h"
#include "src/trace_processor/importers/proto/gpu_event_parser.h"
#include "src/trace_processor/importers/proto/graphics_frame_event_parser.h"
#include "src/trace_processor/importers/proto/proto_importer_module.h"

#include "protos/perfetto/trace/gpu/gpu_counter_event.pbzero.h"
#include "protos/perfetto/trace/trace_packet.pbzero.h"

namespace perfetto::trace_processor {

class GraphicsEventModule : public ProtoImporterModule {
 public:
  explicit GraphicsEventModule(ProtoImporterModuleContext* module_context,
                               TraceProcessorContext* context);

  ~GraphicsEventModule() override;

  ModuleResult TokenizePacket(const TokenizePacketArgs& args) override;

  void ParseField(const ParseFieldArgs& args) override;

 private:
  // Parses the GpuCounterDescriptor portion of a GpuCounterEvent (if present)
  // at tokenization time: interns the counter tracks and inserts the counter
  // groups, caching the resulting counter_id -> track mapping (on the
  // sequence's GpuCounterSequenceState for the interned path, or on the
  // module-global `legacy_gpu_counters_` for the inline path) for the parse
  // stage. `packet_offset` is the byte offset of the packet, used for error
  // reporting.
  void TokenizeGpuCounterEvent(
      PacketSequenceStateGeneration* state,
      size_t packet_offset,
      const protos::pbzero::GpuCounterEvent::Decoder& event);

  // Pushes the sample values of a GpuCounterEvent at parse time, resolving the
  // counter_id -> track mapping built at tokenization time.
  void ParseGpuCounterEvent(int64_t ts,
                            PacketSequenceStateGeneration* state,
                            protozero::ConstBytes);

  TraceProcessorContext* const context_;
  GpuEventParser parser_;
  GraphicsFrameEventParser frame_parser_;
  FrameTimelineEventParser frame_timeline_parser_;

  const StringId counter_id_key_id_;
  const StringId counter_name_key_id_;

  // counter_id -> track info for the legacy inline counter_descriptor path
  // (mode 1). counter_ids are global in this mode, so the map is shared across
  // all packet sequences (this module instance is per-trace).
  GpuEventParser::CounterTrackMap legacy_gpu_counters_;
};

}  // namespace perfetto::trace_processor

#endif  // SRC_TRACE_PROCESSOR_IMPORTERS_PROTO_GRAPHICS_EVENT_MODULE_H_
