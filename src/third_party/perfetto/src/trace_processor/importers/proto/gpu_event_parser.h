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

#ifndef SRC_TRACE_PROCESSOR_IMPORTERS_PROTO_GPU_EVENT_PARSER_H_
#define SRC_TRACE_PROCESSOR_IMPORTERS_PROTO_GPU_EVENT_PARSER_H_

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "perfetto/ext/base/flat_hash_map.h"
#include "perfetto/protozero/field.h"
#include "protos/perfetto/common/gpu_counter_descriptor.pbzero.h"
#include "protos/perfetto/trace/gpu/gpu_counter_event.pbzero.h"
#include "protos/perfetto/trace/gpu/gpu_render_stage_event.pbzero.h"
#include "src/trace_processor/importers/common/args_tracker.h"
#include "src/trace_processor/importers/proto/gpu_counter_sequence_state.h"
#include "src/trace_processor/importers/proto/packet_sequence_state_generation.h"
#include "src/trace_processor/importers/proto/vulkan_memory_tracker.h"
#include "src/trace_processor/storage/trace_storage.h"

#include "protos/perfetto/trace/gpu/vulkan_memory_event.pbzero.h"
#include "src/trace_processor/tables/counter_tables_py.h"

namespace perfetto {

namespace protos::pbzero {
class GpuRenderStageEvent_Decoder;
}  // namespace protos::pbzero

namespace trace_processor {

class TraceProcessorContext;

struct ProtoEnumHasher {
  template <typename T>
  std::size_t operator()(T t) const {
    return static_cast<std::size_t>(t);
  }
};

// Class for parsing graphics related events.
class GpuEventParser {
 public:
  using ConstBytes = protozero::ConstBytes;
  using VulkanMemoryEventSource = protos::pbzero::VulkanMemoryEvent::Source;
  using VulkanMemoryEventOperation =
      protos::pbzero::VulkanMemoryEvent::Operation;
  explicit GpuEventParser(TraceProcessorContext*);

  // GPU counter descriptor helpers, used at tokenization time by
  // GraphicsEventModule to turn a GpuCounterDescriptor into tracks and counter
  // groups.
  struct GroupMetadata {
    StringId name;
    StringId description;
  };
  using GroupMetadataMap = base::FlatHashMap<int32_t, GroupMetadata>;
  using CounterTrackMap =
      base::FlatHashMap<uint32_t, GpuCounterSequenceState::CounterTrackInfo>;
  TrackId InternGpuCounterTrack(
      int32_t gpu_id,
      const protos::pbzero::GpuCounterDescriptor::GpuCounterSpec::Decoder&
          spec);
  GroupMetadataMap BuildGroupMetadata(
      const protos::pbzero::GpuCounterDescriptor::Decoder& desc);
  void InsertCounterGroups(
      TrackId track_id,
      const protos::pbzero::GpuCounterDescriptor::GpuCounterSpec::Decoder& spec,
      const GroupMetadataMap& group_metadata);
  void InsertCustomCounterGroups(
      const protos::pbzero::GpuCounterDescriptor::Decoder& desc,
      const base::FlatHashMap<uint32_t, TrackId>& counter_id_to_track);

  // Pushes the sample values of a GpuCounterEvent at parse time, using an
  // already-resolved counter_id -> track mapping (built at tokenization time).
  // If `report_missing` is set, counters with no matching entry in the map are
  // counted as gpu_counters_invalid_spec (used by the interned path).
  void PushGpuCounterValues(
      int64_t ts,
      const CounterTrackMap& counter_map,
      bool report_missing,
      const protos::pbzero::GpuCounterEvent::Decoder& event);
  void ParseGpuRenderStageEvent(int64_t ts,
                                PacketSequenceStateGeneration*,
                                ConstBytes);
  void ParseGraphicsFrameEvent(int64_t timestamp, ConstBytes);
  void ParseGpuLog(int64_t ts, ConstBytes);

  void ParseVulkanMemoryEvent(PacketSequenceStateGeneration*, ConstBytes);
  void UpdateVulkanMemoryAllocationCounters(
      UniquePid,
      const protos::pbzero::VulkanMemoryEvent::Decoder&);

  void ParseVulkanApiEvent(int64_t, ConstBytes);

  void ParseGpuMemTotalEvent(int64_t, ConstBytes);

 private:
  StringId GetFullStageName(
      PacketSequenceStateGeneration* sequence_state,
      const protos::pbzero::GpuRenderStageEvent_Decoder& event) const;
  void InsertTrackForUninternedRenderStage(
      uint32_t gpu_id,
      uint32_t id,
      const protos::pbzero::GpuRenderStageEvent::Specifications::Description::
          Decoder&);
  std::optional<std::string> FindDebugName(int32_t vk_object_type,
                                           uint64_t vk_handle) const;
  StringId ParseRenderSubpasses(
      const protos::pbzero::GpuRenderStageEvent_Decoder& event) const;

  // GPU counter helpers.
  StringId FormatCounterUnit(
      const protos::pbzero::GpuCounterDescriptor::GpuCounterSpec::Decoder&
          spec);
  // Pushes a counter sample for a GPU counter, handling both the
  // backwards-looking and forwards-looking conventions (see
  // GpuCounterSpec.ValueDirection).
  void PushGpuCounterValue(int64_t ts,
                           double value,
                           TrackId track_id,
                           bool forwards_looking,
                           std::optional<tables::CounterTable::Id>* last_id);

  TraceProcessorContext* const context_;
  VulkanMemoryTracker vulkan_memory_tracker_;

  const StringId context_id_id_;
  const StringId render_target_id_;
  const StringId render_target_name_id_;
  const StringId render_pass_id_;
  const StringId render_pass_name_id_;
  const StringId render_subpasses_id_;
  const StringId command_buffer_id_;
  const StringId command_buffers_id_;
  const StringId command_buffer_name_id_;
  const StringId frame_id_id_;
  const StringId submission_id_id_;
  const StringId hw_queue_id_id_;
  const StringId upid_id_;
  const StringId pid_id_;
  const StringId tid_id_;

  // Track-level last_id for GPU counters. Used by both the interned and the
  // legacy inline descriptor paths when pushing samples. Key: TrackId.
  base::FlatHashMap<TrackId, std::optional<tables::CounterTable::Id>>
      gpu_counter_last_id_;

  // For GpuRenderStageEvent
  struct HwQueueInfo {
    StringId name;
    StringId description;
  };
  const StringId category_id_;
  const StringId kernel_name_id_;
  const StringId kernel_demangled_name_id_;
  const StringId arch_id_;
  const StringId grid_x_id_;
  const StringId grid_y_id_;
  const StringId grid_z_id_;
  const StringId workgroup_x_id_;
  const StringId workgroup_y_id_;
  const StringId workgroup_z_id_;
  const StringId description_id_;
  const StringId correlation_id_;
  std::vector<std::optional<HwQueueInfo>> gpu_hw_queue_ids_;
  base::FlatHashMap<uint64_t, bool> gpu_hw_queue_ids_name_to_set_;

  void ParseExtraComputeArg(PacketSequenceStateGeneration* sequence_state,
                            protozero::ConstBytes bytes,
                            ArgsTracker::BoundInserter* inserter);
  void ParseComputeKernel(PacketSequenceStateGeneration* sequence_state,
                          uint64_t kernel_iid,
                          ArgsTracker::BoundInserter* inserter);
  void ParseComputeKernelLaunch(PacketSequenceStateGeneration* sequence_state,
                                protozero::ConstBytes bytes,
                                ArgsTracker::BoundInserter* inserter);

  void InternGpuContext(
      uint64_t context_id,
      const protos::pbzero::InternedGraphicsContext::Decoder& ctx);

  // Map of stage ID -> pair(stage name, stage description)
  std::vector<std::pair<StringId, StringId>> gpu_render_stage_ids_;

  // Graphics contexts already inserted into gpu_context table.
  base::FlatHashMap<uint64_t, bool> gpu_contexts_inserted_;

  // For VulkanMemoryEvent
  std::unordered_map<protos::pbzero::VulkanMemoryEvent::AllocationScope,
                     int64_t /*counter_value*/,
                     ProtoEnumHasher>
      vulkan_driver_memory_counters_;
  std::unordered_map<uint32_t /*memory_type*/, int64_t /*counter_value*/>
      vulkan_device_memory_counters_allocate_;
  std::unordered_map<uint32_t /*memory_type*/, int64_t /*counter_value*/>
      vulkan_device_memory_counters_bind_;

  // For GpuLog
  const StringId tag_id_;
  const StringId log_message_id_;
  std::array<StringId, 7> log_severity_ids_;

  // For Vulkan events.
  // For VulkanApiEvent.VkDebugUtilsObjectName.
  // Map of vk handle -> vk object name.
  using DebugMarkerMap = std::unordered_map<uint64_t, std::string>;

  // Map of VkObjectType -> DebugMarkerMap.
  std::unordered_map<int32_t, DebugMarkerMap> debug_marker_names_;

  // For VulkanApiEvent.VkQueueSubmit.
  StringId vk_event_track_id_;
  StringId vk_queue_submit_id_;
};
}  // namespace trace_processor
}  // namespace perfetto

#endif  // SRC_TRACE_PROCESSOR_IMPORTERS_PROTO_GPU_EVENT_PARSER_H_
