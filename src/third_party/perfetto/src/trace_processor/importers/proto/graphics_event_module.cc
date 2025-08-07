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

#include "src/trace_processor/importers/proto/graphics_event_module.h"
#include "src/trace_processor/importers/common/trace_parser.h"

namespace perfetto {
namespace trace_processor {

using perfetto::protos::pbzero::TracePacket;

GraphicsEventModule::GraphicsEventModule(TraceProcessorContext* context)
    : parser_(context),
      frame_parser_(context),
      frame_timeline_parser_(context) {
  RegisterForField(TracePacket::kFrameTimelineEventFieldNumber, context);
  RegisterForField(TracePacket::kGpuCounterEventFieldNumber, context);
  RegisterForField(TracePacket::kGpuRenderStageEventFieldNumber, context);
  RegisterForField(TracePacket::kGpuLogFieldNumber, context);
  RegisterForField(TracePacket::kGpuMemTotalEventFieldNumber, context);
  RegisterForField(TracePacket::kGraphicsFrameEventFieldNumber, context);
  RegisterForField(TracePacket::kVulkanMemoryEventFieldNumber, context);
  RegisterForField(TracePacket::kVulkanApiEventFieldNumber, context);
}

GraphicsEventModule::~GraphicsEventModule() = default;

void GraphicsEventModule::ParseTracePacketData(
    const TracePacket::Decoder& decoder,
    int64_t ts,
    const TracePacketData& data,
    uint32_t field_id) {
  switch (field_id) {
    case TracePacket::kFrameTimelineEventFieldNumber:
      frame_timeline_parser_.ParseFrameTimelineEvent(
          ts, decoder.frame_timeline_event());
      return;
    case TracePacket::kGpuCounterEventFieldNumber:
      parser_.ParseGpuCounterEvent(ts, decoder.gpu_counter_event());
      return;
    case TracePacket::kGpuRenderStageEventFieldNumber:
      parser_.ParseGpuRenderStageEvent(ts, data.sequence_state.get(),
                                       decoder.gpu_render_stage_event());
      return;
    case TracePacket::kGpuLogFieldNumber:
      parser_.ParseGpuLog(ts, decoder.gpu_log());
      return;
    case TracePacket::kGraphicsFrameEventFieldNumber:
      frame_parser_.ParseGraphicsFrameEvent(ts, decoder.graphics_frame_event());
      return;
    case TracePacket::kVulkanMemoryEventFieldNumber:
      parser_.ParseVulkanMemoryEvent(data.sequence_state.get(),
                                     decoder.vulkan_memory_event());
      return;
    case TracePacket::kVulkanApiEventFieldNumber:
      parser_.ParseVulkanApiEvent(ts, decoder.vulkan_api_event());
      return;
    case TracePacket::kGpuMemTotalEventFieldNumber:
      parser_.ParseGpuMemTotalEvent(ts, decoder.gpu_mem_total_event());
      return;
  }
}

}  // namespace trace_processor
}  // namespace perfetto
