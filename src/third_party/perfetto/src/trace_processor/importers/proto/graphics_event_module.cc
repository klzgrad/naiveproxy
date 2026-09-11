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

#include <cstdint>
#include <utility>

#include "perfetto/ext/base/flat_hash_map.h"
#include "src/trace_processor/importers/common/args_tracker.h"
#include "src/trace_processor/importers/common/import_logs_tracker.h"
#include "src/trace_processor/importers/common/parser_types.h"
#include "src/trace_processor/importers/common/stats_tracker.h"
#include "src/trace_processor/importers/proto/gpu_counter_sequence_state.h"
#include "src/trace_processor/importers/proto/packet_sequence_state_generation.h"
#include "src/trace_processor/storage/stats.h"
#include "src/trace_processor/storage/trace_storage.h"
#include "src/trace_processor/types/trace_processor_context.h"

#include "protos/perfetto/common/gpu_counter_descriptor.pbzero.h"
#include "protos/perfetto/trace/gpu/gpu_counter_event.pbzero.h"
#include "protos/perfetto/trace/interned_data/interned_data.pbzero.h"
#include "protos/perfetto/trace/trace_packet.pbzero.h"
#include "protos/third_party/android/frameworks/native/tracing/frameworks_native_trace_packet.pbzero.h"

namespace perfetto::trace_processor {

using com::android::internal::pbzero::FrameworksNativeTracePacket;
using perfetto::protos::pbzero::GpuCounterDescriptor;
using perfetto::protos::pbzero::GpuCounterEvent;
using perfetto::protos::pbzero::InternedData;
using perfetto::protos::pbzero::InternedGpuCounterDescriptor;
using perfetto::protos::pbzero::TracePacket;

GraphicsEventModule::GraphicsEventModule(
    ProtoImporterModuleContext* module_context,
    TraceProcessorContext* context)
    : ProtoImporterModule(module_context),
      context_(context),
      parser_(context),
      frame_parser_(context),
      frame_timeline_parser_(context),
      counter_id_key_id_(context->storage->InternString("counter_id")),
      counter_name_key_id_(context->storage->InternString("counter_name")) {
  RegisterForField(FrameworksNativeTracePacket::kFrameTimelineEventFieldNumber);
  RegisterForField(TracePacket::kGpuCounterEventFieldNumber);
  RegisterForField(TracePacket::kGpuRenderStageEventFieldNumber);
  RegisterForField(TracePacket::kGpuLogFieldNumber);
  RegisterForField(TracePacket::kGpuMemTotalEventFieldNumber);
  RegisterForField(TracePacket::kGraphicsFrameEventFieldNumber);
  RegisterForField(TracePacket::kVulkanMemoryEventFieldNumber);
  RegisterForField(TracePacket::kVulkanApiEventFieldNumber);
}

GraphicsEventModule::~GraphicsEventModule() = default;

ModuleResult GraphicsEventModule::TokenizePacket(
    const TokenizePacketArgs& args) {
  if (args.field.id() != TracePacket::kGpuCounterEventFieldNumber) {
    return ModuleResult::Ignored();
  }

  // A GpuCounterEvent has two independent sections that are handled in
  // different stages:
  //  - the descriptor section (counter_descriptor / counter_descriptor_iid) is
  //    handled here at tokenization time (track + counter group setup);
  //  - the event section (the counter samples) is handled at parse time.
  // The descriptor section is processed unconditionally, even if the packet has
  // no timestamp.
  GpuCounterEvent::Decoder event(
      args.field.Cast<TracePacket::kGpuCounterEvent>());
  TokenizeGpuCounterEvent(args.state.get(), args.packet->offset(), event);

  // Only the event section needs a timestamp (the samples have to be sorted). A
  // descriptor-only packet legitimately has no samples and no timestamp, so
  // only flag/drop the packet when it actually carries samples. Returning a
  // non-Ignored result stops it from reaching the sorter.
  if (event.counters() && !args.decoder.has_timestamp()) {
    context_->import_logs_tracker->RecordTokenizationLog(
        stats::gpu_counters_missing_timestamp, args.packet->offset());
    return ModuleResult::Handled();
  }

  // Let the packet flow through to the sorter so its samples are pushed at
  // parse time.
  return ModuleResult::Ignored();
}

void GraphicsEventModule::TokenizeGpuCounterEvent(
    PacketSequenceStateGeneration* state,
    size_t packet_offset,
    const GpuCounterEvent::Decoder& event) {
  auto* gpu_counter_state = state->GetCustomState<GpuCounterSequenceState>();

  // Interned descriptor path: parse the descriptor once per iid, interning the
  // tracks and inserting the counter groups. The resulting counter_id -> track
  // mapping is cached on the sequence's IncrementalState (alongside the
  // interned-data table the descriptor was looked up from), so two producers
  // picking the same iid on different sequences each get their own cache.
  if (event.has_counter_descriptor_iid()) {
    auto iid = event.counter_descriptor_iid();
    if (gpu_counter_state->descriptors.Find(iid)) {
      return;
    }

    auto* interned = state->LookupInternedMessage<
        InternedData::kGpuCounterDescriptorsFieldNumber,
        InternedGpuCounterDescriptor>(iid);
    if (!interned || !interned->has_counter_descriptor()) {
      context_->stats_tracker->IncrementStats(stats::gpu_counters_invalid_spec);
      return;
    }

    GpuCounterDescriptor::Decoder desc(interned->counter_descriptor());
    auto gpu_id = interned->gpu_id();
    auto group_metadata = parser_.BuildGroupMetadata(desc);

    base::FlatHashMap<uint32_t, GpuCounterSequenceState::CounterTrackInfo>
        counter_map;
    base::FlatHashMap<uint32_t, TrackId> counter_id_to_track;
    for (auto spec_it = desc.specs(); spec_it; ++spec_it) {
      GpuCounterDescriptor::GpuCounterSpec::Decoder spec(*spec_it);
      if (!spec.has_counter_id() || !spec.has_name()) {
        continue;
      }
      auto track_id = parser_.InternGpuCounterTrack(gpu_id, spec);
      if (counter_id_to_track.Insert(spec.counter_id(), track_id).second) {
        parser_.InsertCounterGroups(track_id, spec, group_metadata);
      }
      bool forwards_looking = spec.value_direction() ==
                              GpuCounterDescriptor::GpuCounterSpec::
                                  VALUE_DIRECTION_FORWARDS_LOOKING;
      counter_map.Insert(spec.counter_id(),
                         GpuCounterSequenceState::CounterTrackInfo{
                             track_id, forwards_looking});
    }
    parser_.InsertCustomCounterGroups(desc, counter_id_to_track);
    gpu_counter_state->descriptors.Insert(iid, std::move(counter_map));
    return;
  }

  // Legacy inline counter_descriptor path. counter_ids are global here, so the
  // mapping is cached on the module-global `legacy_gpu_counters_`.
  if (event.has_counter_descriptor()) {
    GpuCounterDescriptor::Decoder descriptor(event.counter_descriptor());
    auto group_metadata = parser_.BuildGroupMetadata(descriptor);
    base::FlatHashMap<uint32_t, TrackId> counter_id_to_track;
    for (auto it = descriptor.specs(); it; ++it) {
      GpuCounterDescriptor::GpuCounterSpec::Decoder spec(*it);
      if (!spec.has_counter_id()) {
        context_->import_logs_tracker->RecordTokenizationLog(
            stats::gpu_counters_invalid_spec, packet_offset);
        continue;
      }
      if (!spec.has_name()) {
        context_->import_logs_tracker->RecordTokenizationLog(
            stats::gpu_counters_invalid_spec, packet_offset);
        continue;
      }

      auto counter_id = spec.counter_id();
      if (legacy_gpu_counters_.Find(counter_id)) {
        context_->import_logs_tracker->RecordTokenizationLog(
            stats::gpu_counters_invalid_spec, packet_offset,
            [this, counter_id, &spec](ArgsTracker::BoundInserter& inserter) {
              inserter.AddArg(counter_id_key_id_,
                              Variadic::UnsignedInteger(counter_id));
              inserter.AddArg(counter_name_key_id_,
                              Variadic::String(context_->storage->InternString(
                                  spec.name())));
            });
        continue;
      }

      auto gpu_id = event.gpu_id();
      auto track_id = parser_.InternGpuCounterTrack(gpu_id, spec);
      parser_.InsertCounterGroups(track_id, spec, group_metadata);
      bool forwards_looking = spec.value_direction() ==
                              GpuCounterDescriptor::GpuCounterSpec::
                                  VALUE_DIRECTION_FORWARDS_LOOKING;
      legacy_gpu_counters_.Insert(counter_id,
                                  GpuCounterSequenceState::CounterTrackInfo{
                                      track_id, forwards_looking});
      counter_id_to_track.Insert(counter_id, track_id);
    }
    parser_.InsertCustomCounterGroups(descriptor, counter_id_to_track);
  }
}

void GraphicsEventModule::ParseGpuCounterEvent(
    int64_t ts,
    PacketSequenceStateGeneration* state,
    protozero::ConstBytes blob) {
  GpuCounterEvent::Decoder event(blob);
  if (event.has_counter_descriptor_iid()) {
    auto* gpu_counter_state = state->GetCustomState<GpuCounterSequenceState>();
    const auto* counter_map =
        gpu_counter_state->descriptors.Find(event.counter_descriptor_iid());
    if (!counter_map) {
      // The descriptor was missing or invalid; already reported at
      // tokenization time.
      return;
    }
    parser_.PushGpuCounterValues(ts, *counter_map, /*report_missing=*/true,
                                 event);
    return;
  }
  parser_.PushGpuCounterValues(ts, legacy_gpu_counters_,
                               /*report_missing=*/false, event);
}

void GraphicsEventModule::ParseField(const ParseFieldArgs& args) {
  switch (args.field.id()) {
    case FrameworksNativeTracePacket::kFrameTimelineEventFieldNumber:
      frame_timeline_parser_.ParseFrameTimelineEvent(
          args.ts,
          args.field.Cast<FrameworksNativeTracePacket::kFrameTimelineEvent>());
      return;
    case TracePacket::kGpuCounterEventFieldNumber:
      ParseGpuCounterEvent(args.ts, args.data.sequence_state.get(),
                           args.field.Cast<TracePacket::kGpuCounterEvent>());
      return;
    case TracePacket::kGpuRenderStageEventFieldNumber:
      parser_.ParseGpuRenderStageEvent(
          args.ts, args.data.sequence_state.get(),
          args.field.Cast<TracePacket::kGpuRenderStageEvent>());
      return;
    case TracePacket::kGpuLogFieldNumber:
      parser_.ParseGpuLog(args.ts, args.field.Cast<TracePacket::kGpuLog>());
      return;
    case TracePacket::kGraphicsFrameEventFieldNumber:
      frame_parser_.ParseGraphicsFrameEvent(
          args.ts, args.field.Cast<TracePacket::kGraphicsFrameEvent>());
      return;
    case TracePacket::kVulkanMemoryEventFieldNumber:
      parser_.ParseVulkanMemoryEvent(
          args.data.sequence_state.get(),
          args.field.Cast<TracePacket::kVulkanMemoryEvent>());
      return;
    case TracePacket::kVulkanApiEventFieldNumber:
      parser_.ParseVulkanApiEvent(
          args.ts, args.field.Cast<TracePacket::kVulkanApiEvent>());
      return;
    case TracePacket::kGpuMemTotalEventFieldNumber:
      parser_.ParseGpuMemTotalEvent(
          args.ts, args.field.Cast<TracePacket::kGpuMemTotalEvent>());
      return;
  }
}

}  // namespace perfetto::trace_processor
