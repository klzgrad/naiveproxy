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

#include "src/trace_processor/importers/proto/gpu_event_parser.h"

#include <array>
#include <cinttypes>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "perfetto/base/logging.h"
#include "perfetto/ext/base/fixed_string_writer.h"
#include "perfetto/ext/base/string_utils.h"
#include "perfetto/ext/base/string_view.h"
#include "perfetto/protozero/field.h"
#include "src/trace_processor/importers/common/args_tracker.h"
#include "src/trace_processor/importers/common/event_tracker.h"
#include "src/trace_processor/importers/common/process_tracker.h"
#include "src/trace_processor/importers/common/slice_tracker.h"
#include "src/trace_processor/importers/common/track_compressor.h"
#include "src/trace_processor/importers/common/track_tracker.h"
#include "src/trace_processor/importers/common/tracks.h"
#include "src/trace_processor/importers/common/tracks_common.h"
#include "src/trace_processor/importers/proto/packet_sequence_state_generation.h"
#include "src/trace_processor/importers/proto/vulkan_memory_tracker.h"
#include "src/trace_processor/storage/stats.h"
#include "src/trace_processor/storage/trace_storage.h"
#include "src/trace_processor/tables/profiler_tables_py.h"
#include "src/trace_processor/tables/track_tables_py.h"
#include "src/trace_processor/types/trace_processor_context.h"
#include "src/trace_processor/types/variadic.h"

#include "protos/perfetto/common/gpu_counter_descriptor.pbzero.h"
#include "protos/perfetto/trace/android/gpu_mem_event.pbzero.h"
#include "protos/perfetto/trace/gpu/gpu_counter_event.pbzero.h"
#include "protos/perfetto/trace/gpu/gpu_log.pbzero.h"
#include "protos/perfetto/trace/gpu/gpu_render_stage_event.pbzero.h"
#include "protos/perfetto/trace/gpu/vulkan_api_event.pbzero.h"
#include "protos/perfetto/trace/gpu/vulkan_memory_event.pbzero.h"
#include "protos/perfetto/trace/interned_data/interned_data.pbzero.h"

namespace perfetto::trace_processor {

namespace {

// https://www.khronos.org/registry/vulkan/specs/1.1-extensions/man/html/VkObjectType.html
enum VkObjectType {
  VK_OBJECT_TYPE_UNKNOWN = 0,
  VK_OBJECT_TYPE_INSTANCE = 1,
  VK_OBJECT_TYPE_PHYSICAL_DEVICE = 2,
  VK_OBJECT_TYPE_DEVICE = 3,
  VK_OBJECT_TYPE_QUEUE = 4,
  VK_OBJECT_TYPE_SEMAPHORE = 5,
  VK_OBJECT_TYPE_COMMAND_BUFFER = 6,
  VK_OBJECT_TYPE_FENCE = 7,
  VK_OBJECT_TYPE_DEVICE_MEMORY = 8,
  VK_OBJECT_TYPE_BUFFER = 9,
  VK_OBJECT_TYPE_IMAGE = 10,
  VK_OBJECT_TYPE_EVENT = 11,
  VK_OBJECT_TYPE_QUERY_POOL = 12,
  VK_OBJECT_TYPE_BUFFER_VIEW = 13,
  VK_OBJECT_TYPE_IMAGE_VIEW = 14,
  VK_OBJECT_TYPE_SHADER_MODULE = 15,
  VK_OBJECT_TYPE_PIPELINE_CACHE = 16,
  VK_OBJECT_TYPE_PIPELINE_LAYOUT = 17,
  VK_OBJECT_TYPE_RENDER_PASS = 18,
  VK_OBJECT_TYPE_PIPELINE = 19,
  VK_OBJECT_TYPE_DESCRIPTOR_SET_LAYOUT = 20,
  VK_OBJECT_TYPE_SAMPLER = 21,
  VK_OBJECT_TYPE_DESCRIPTOR_POOL = 22,
  VK_OBJECT_TYPE_DESCRIPTOR_SET = 23,
  VK_OBJECT_TYPE_FRAMEBUFFER = 24,
  VK_OBJECT_TYPE_COMMAND_POOL = 25,
  VK_OBJECT_TYPE_SAMPLER_YCBCR_CONVERSION = 1000156000,
  VK_OBJECT_TYPE_DESCRIPTOR_UPDATE_TEMPLATE = 1000085000,
  VK_OBJECT_TYPE_SURFACE_KHR = 1000000000,
  VK_OBJECT_TYPE_SWAPCHAIN_KHR = 1000001000,
  VK_OBJECT_TYPE_DISPLAY_KHR = 1000002000,
  VK_OBJECT_TYPE_DISPLAY_MODE_KHR = 1000002001,
  VK_OBJECT_TYPE_DEBUG_REPORT_CALLBACK_EXT = 1000011000,
  VK_OBJECT_TYPE_OBJECT_TABLE_NVX = 1000086000,
  VK_OBJECT_TYPE_INDIRECT_COMMANDS_LAYOUT_NVX = 1000086001,
  VK_OBJECT_TYPE_DEBUG_UTILS_MESSENGER_EXT = 1000128000,
  VK_OBJECT_TYPE_VALIDATION_CACHE_EXT = 1000160000,
  VK_OBJECT_TYPE_ACCELERATION_STRUCTURE_NV = 1000165000,
  VK_OBJECT_TYPE_PERFORMANCE_CONFIGURATION_INTEL = 1000210000,
  VK_OBJECT_TYPE_DESCRIPTOR_UPDATE_TEMPLATE_KHR =
      VK_OBJECT_TYPE_DESCRIPTOR_UPDATE_TEMPLATE,
  VK_OBJECT_TYPE_SAMPLER_YCBCR_CONVERSION_KHR =
      VK_OBJECT_TYPE_SAMPLER_YCBCR_CONVERSION,
  VK_OBJECT_TYPE_MAX_ENUM = 0x7FFFFFFF
};

using protos::pbzero::GpuCounterDescriptor;
using protos::pbzero::GpuCounterEvent;
using protos::pbzero::GpuRenderStageEvent;
using protos::pbzero::VulkanMemoryEvent;

constexpr auto kRenderStageBlueprint = TrackCompressor::SliceBlueprint(
    "gpu_render_stage",
    tracks::DimensionBlueprints(
        tracks::StringDimensionBlueprint("render_stage_source"),
        tracks::UintDimensionBlueprint("hwqueue_id"),
        tracks::StringIdDimensionBlueprint("hwqueue_name")),
    tracks::DynamicNameBlueprint());

}  // anonymous namespace

GpuEventParser::GpuEventParser(TraceProcessorContext* context)
    : context_(context),
      vulkan_memory_tracker_(context),
      context_id_id_(context->storage->InternString("context_id")),
      render_target_id_(context->storage->InternString("render_target")),
      render_target_name_id_(
          context->storage->InternString("render_target_name")),
      render_pass_id_(context->storage->InternString("render_pass")),
      render_pass_name_id_(context->storage->InternString("render_pass_name")),
      render_subpasses_id_(context->storage->InternString("render_subpasses")),
      command_buffer_id_(context->storage->InternString("command_buffer")),
      command_buffer_name_id_(
          context->storage->InternString("command_buffer_name")),
      frame_id_id_(context->storage->InternString("frame_id")),
      submission_id_id_(context->storage->InternString("submission_id")),
      hw_queue_id_id_(context->storage->InternString("hw_queue_id")),
      upid_id_(context->storage->InternString("upid")),
      pid_id_(context_->storage->InternString("pid")),
      tid_id_(context_->storage->InternString("tid")),
      description_id_(context->storage->InternString("description")),
      correlation_id_(context->storage->InternString("correlation_id")),
      tag_id_(context_->storage->InternString("tag")),
      log_message_id_(context->storage->InternString("message")),
      log_severity_ids_{{context_->storage->InternString("UNSPECIFIED"),
                         context_->storage->InternString("VERBOSE"),
                         context_->storage->InternString("DEBUG"),
                         context_->storage->InternString("INFO"),
                         context_->storage->InternString("WARNING"),
                         context_->storage->InternString("ERROR"),
                         context_->storage->InternString(
                             "UNKNOWN_SEVERITY") /* must be last */}},
      vk_queue_submit_id_(context->storage->InternString("vkQueueSubmit")) {}

void GpuEventParser::TokenizeGpuCounterEvent(ConstBytes blob) {
  GpuCounterEvent::Decoder event(blob);
  if (!event.has_counter_descriptor()) {
    return;
  }
  GpuCounterDescriptor::Decoder descriptor(event.counter_descriptor());
  for (auto it = descriptor.specs(); it; ++it) {
    GpuCounterDescriptor::GpuCounterSpec::Decoder spec(*it);
    if (!spec.has_counter_id()) {
      PERFETTO_ELOG("Counter spec missing counter id");
      context_->storage->IncrementStats(stats::gpu_counters_invalid_spec);
      continue;
    }
    if (!spec.has_name()) {
      context_->storage->IncrementStats(stats::gpu_counters_invalid_spec);
      continue;
    }

    auto counter_id = spec.counter_id();
    auto name = spec.name();
    if (!gpu_counter_state_.Find(counter_id)) {
      auto desc = spec.description();

      StringId unit_id = kNullStringId;
      if (spec.has_numerator_units() || spec.has_denominator_units()) {
        char buffer[1024];
        base::FixedStringWriter unit(buffer, sizeof(buffer));
        for (auto number = spec.numerator_units(); number; ++number) {
          if (unit.pos()) {
            unit.AppendChar(':');
          }
          unit.AppendInt(*number);
        }
        char sep = '/';
        for (auto denom = spec.denominator_units(); denom; ++denom) {
          unit.AppendChar(sep);
          unit.AppendInt(*denom);
          sep = ':';
        }
        unit_id = context_->storage->InternString(unit.GetStringView());
      }

      auto name_id = context_->storage->InternString(name);
      auto desc_id = context_->storage->InternString(desc);
      auto gpu_id = event.has_gpu_id() ? event.gpu_id() : 0;
      auto track_id = context_->track_tracker->InternTrack(
          tracks::kGpuCounterBlueprint, tracks::Dimensions(gpu_id, name),
          tracks::DynamicName(name_id),
          [&, this](ArgsTracker::BoundInserter& inserter) {
            inserter.AddArg(description_id_, Variadic::String(desc_id));
          },
          tracks::DynamicUnit(unit_id));
      auto [cit, inserted] =
          gpu_counter_state_.Insert(counter_id, GpuCounterState{track_id, {}});
      PERFETTO_CHECK(inserted);
      if (spec.has_groups()) {
        for (auto group = spec.groups(); group; ++group) {
          tables::GpuCounterGroupTable::Row row;
          row.group_id = *group;
          row.track_id = track_id;
          context_->storage->mutable_gpu_counter_group_table()->Insert(row);
        }
      } else {
        tables::GpuCounterGroupTable::Row row;
        row.group_id = protos::pbzero::GpuCounterDescriptor::UNCLASSIFIED;
        row.track_id = track_id;
        context_->storage->mutable_gpu_counter_group_table()->Insert(row);
      }
    } else {
      // Either counter spec was repeated or it came after counter data.
      PERFETTO_ELOG("Duplicated counter spec found. (counter_id=%u, name=%s)",
                    counter_id, name.ToStdString().c_str());
      context_->storage->IncrementStats(stats::gpu_counters_invalid_spec);
    }
  }
}

void GpuEventParser::ParseGpuCounterEvent(int64_t ts, ConstBytes blob) {
  GpuCounterEvent::Decoder event(blob);
  for (auto it = event.counters(); it; ++it) {
    GpuCounterEvent::GpuCounter::Decoder counter(*it);
    if (counter.has_counter_id() &&
        (counter.has_int_value() || counter.has_double_value())) {
      auto* state = gpu_counter_state_.Find(counter.counter_id());
      if (!state) {
        continue;
      }
      double counter_val = counter.has_int_value()
                               ? static_cast<double>(counter.int_value())
                               : counter.double_value();
      auto id = context_->event_tracker->PushCounter(ts, 0, state->track_id);
      if (state->last_id) {
        auto row = context_->storage->mutable_counter_table()->FindById(
            *state->last_id);
        row->set_value(counter_val);
      }
      state->last_id = id;
    }
  }
}

StringId GpuEventParser::GetFullStageName(
    PacketSequenceStateGeneration* sequence_state,
    const protos::pbzero::GpuRenderStageEvent_Decoder& event) const {
  StringId stage_name;
  if (event.has_stage_iid()) {
    auto stage_iid = event.stage_iid();
    auto* decoder = sequence_state->LookupInternedMessage<
        protos::pbzero::InternedData::kGpuSpecificationsFieldNumber,
        protos::pbzero::InternedGpuRenderStageSpecification>(stage_iid);
    if (!decoder) {
      return kNullStringId;
    }
    stage_name = context_->storage->InternString(decoder->name());
  } else {
    auto stage_id = static_cast<uint64_t>(event.stage_id());
    if (stage_id < gpu_render_stage_ids_.size()) {
      stage_name = gpu_render_stage_ids_[static_cast<size_t>(stage_id)].first;
    } else {
      base::StackString<64> name("render stage(%" PRIu64 ")", stage_id);
      stage_name = context_->storage->InternString(name.string_view());
    }
  }
  return stage_name;
}

void GpuEventParser::InsertTrackForUninternedRenderStage(
    uint32_t hw_queue_id,
    const GpuRenderStageEvent::Specifications::Description::Decoder& hw_queue) {
  if (!hw_queue.has_name()) {
    return;
  }
  if (hw_queue_id >= gpu_hw_queue_ids_.size()) {
    gpu_hw_queue_ids_.resize(hw_queue_id + 1);
  }

  StringId name = context_->storage->InternString(hw_queue.name());
  StringId description =
      context_->storage->InternString(hw_queue.description());
  gpu_hw_queue_ids_[hw_queue_id] = HwQueueInfo{name, description};

  // Most weell behaved traces will not have to set the name.
  if (gpu_hw_queue_ids_name_to_set_.size() == 0) {
    return;
  }

  // The track might have been created before with a placeholder name.
  // We need to update it if `gpu_hw_queue_ids_name_to_set_` says so.
  auto* it = gpu_hw_queue_ids_name_to_set_.Find(hw_queue_id);
  if (!it || !*it) {
    return;
  }

  // Mark this as handled.
  *it = false;

  auto factory = context_->track_compressor->CreateTrackFactory(
      kRenderStageBlueprint,
      tracks::Dimensions("id", hw_queue_id, kNullStringId),
      tracks::DynamicName(name),
      [&, this](ArgsTracker::BoundInserter& inserter) {
        inserter.AddArg(description_id_, Variadic::String(description));
      });
  TrackId track_id = context_->track_compressor->DefaultTrack(factory);
  auto rr = *context_->storage->mutable_track_table()->FindById(track_id);
  rr.set_name(name);

  PERFETTO_DCHECK(!rr.source_arg_set_id());
  ArgsTracker args_tracker(context_);
  auto inserter = args_tracker.AddArgsTo(track_id);
  inserter.AddArg(description_id_, Variadic::String(description));
}

std::optional<std::string> GpuEventParser::FindDebugName(
    int32_t vk_object_type,
    uint64_t vk_handle) const {
  auto map = debug_marker_names_.find(vk_object_type);
  if (map == debug_marker_names_.end()) {
    return std::nullopt;
  }

  auto name = map->second.find(vk_handle);
  if (name == map->second.end()) {
    return std::nullopt;
  }
  return name->second;
}

StringId GpuEventParser::ParseRenderSubpasses(
    const protos::pbzero::GpuRenderStageEvent_Decoder& event) const {
  if (!event.has_render_subpass_index_mask()) {
    return kNullStringId;
  }
  char buf[256];
  base::FixedStringWriter writer(buf, sizeof(buf));
  uint32_t bit_index = 0;
  bool first = true;
  for (auto it = event.render_subpass_index_mask(); it; ++it) {
    auto subpasses_bits = *it;
    do {
      if ((subpasses_bits & 1) != 0) {
        if (!first) {
          writer.AppendChar(',');
        }
        first = false;
        writer.AppendUnsignedInt(bit_index);
      }
      subpasses_bits >>= 1;
      ++bit_index;
    } while (subpasses_bits != 0);
    // Round up to the next multiple of 64.
    bit_index = ((bit_index - 1) / 64 + 1) * 64;
  }
  return context_->storage->InternString(writer.GetStringView());
}

void GpuEventParser::ParseGpuRenderStageEvent(
    int64_t ts,
    PacketSequenceStateGeneration* sequence_state,
    ConstBytes blob) {
  GpuRenderStageEvent::Decoder event(blob);

  int32_t pid = 0;
  if (event.has_specifications()) {
    GpuRenderStageEvent::Specifications::Decoder spec(event.specifications());
    uint32_t hw_queue_id = 0;
    for (auto it = spec.hw_queue(); it; ++it) {
      GpuRenderStageEvent::Specifications::Description::Decoder hw_queue(*it);
      InsertTrackForUninternedRenderStage(hw_queue_id++, hw_queue);
    }
    for (auto it = spec.stage(); it; ++it) {
      GpuRenderStageEvent::Specifications::Description::Decoder stage(*it);
      if (stage.has_name()) {
        gpu_render_stage_ids_.emplace_back(
            context_->storage->InternString(stage.name()),
            context_->storage->InternString(stage.description()));
      }
    }
    if (spec.has_context_spec()) {
      GpuRenderStageEvent::Specifications::ContextSpec::Decoder context_spec(
          spec.context_spec());
      if (context_spec.has_pid()) {
        pid = context_spec.pid();
      }
    }
  }

  if (event.has_context()) {
    uint64_t context_id = event.context();
    auto* decoder = sequence_state->LookupInternedMessage<
        protos::pbzero::InternedData::kGraphicsContextsFieldNumber,
        protos::pbzero::InternedGraphicsContext>(context_id);
    if (decoder) {
      pid = decoder->pid();
    }
  }

  if (event.has_event_id()) {
    StringId track_name = kNullStringId;
    StringId track_description = kNullStringId;
    StringId dimension_name = kNullStringId;
    uint64_t hw_queue_id = 0;
    const char* source = nullptr;

    if (event.has_hw_queue_iid()) {
      source = "iid";
      hw_queue_id = event.hw_queue_iid();
      auto* decoder = sequence_state->LookupInternedMessage<
          protos::pbzero::InternedData::kGpuSpecificationsFieldNumber,
          protos::pbzero::InternedGpuRenderStageSpecification>(hw_queue_id);
      if (!decoder) {
        return;
      }
      track_name = context_->storage->InternString(decoder->name());
      dimension_name = track_name;
      if (decoder->description().size > 0) {
        track_description =
            context_->storage->InternString(decoder->description());
      }
    } else {
      source = "id";
      hw_queue_id = static_cast<uint32_t>(event.hw_queue_id());
      if (hw_queue_id < gpu_hw_queue_ids_.size() &&
          gpu_hw_queue_ids_[hw_queue_id].has_value()) {
        track_name = gpu_hw_queue_ids_[hw_queue_id]->name;
        track_description = gpu_hw_queue_ids_[hw_queue_id]->description;
        dimension_name = track_name;
      } else {
        // If the event has a hw_queue_id that does not have a Specification,
        // create a new track for it. Use kNullStringId as dimension to keep it
        // stable.
        base::StackString<64> placeholder_name("Unknown GPU Queue %" PRIu64,
                                               hw_queue_id);
        track_name =
            context_->storage->InternString(placeholder_name.string_view());
        dimension_name = kNullStringId;
        gpu_hw_queue_ids_name_to_set_.Insert(hw_queue_id, true);
      }
    }

    auto render_target_name =
        FindDebugName(VK_OBJECT_TYPE_FRAMEBUFFER, event.render_target_handle());
    auto render_target_name_id = render_target_name.has_value()
                                     ? context_->storage->InternString(
                                           render_target_name.value().c_str())
                                     : kNullStringId;
    auto render_pass_name =
        FindDebugName(VK_OBJECT_TYPE_RENDER_PASS, event.render_pass_handle());
    auto render_pass_name_id =
        render_pass_name.has_value()
            ? context_->storage->InternString(render_pass_name.value().c_str())
            : kNullStringId;

    auto command_buffer_name = FindDebugName(VK_OBJECT_TYPE_COMMAND_BUFFER,
                                             event.command_buffer_handle());
    auto command_buffer_name_id = command_buffer_name.has_value()
                                      ? context_->storage->InternString(
                                            command_buffer_name.value().c_str())
                                      : kNullStringId;
    TrackId track_id = context_->track_compressor->InternScoped(
        kRenderStageBlueprint,
        tracks::Dimensions(base::StringView(source),
                           static_cast<uint32_t>(hw_queue_id), dimension_name),
        ts, static_cast<int64_t>(event.duration()),
        tracks::DynamicName(track_name),
        [&](ArgsTracker::BoundInserter& inserter) {
          if (track_description != kNullStringId) {
            inserter.AddArg(description_id_,
                            Variadic::String(track_description));
          }
        });

    StringId name_id = GetFullStageName(sequence_state, event);
    context_->slice_tracker->Scoped(
        ts, track_id, kNullStringId, name_id,
        static_cast<int64_t>(event.duration()),
        [&](ArgsTracker::BoundInserter* inserter) {
          if (event.has_stage_iid()) {
            auto stage_iid = static_cast<size_t>(event.stage_iid());
            auto* decoder = sequence_state->LookupInternedMessage<
                protos::pbzero::InternedData::kGpuSpecificationsFieldNumber,
                protos::pbzero::InternedGpuRenderStageSpecification>(stage_iid);
            if (decoder) {
              // TODO: Add RenderStageCategory to gpu_slice table.
              inserter->AddArg(description_id_,
                               Variadic::String(context_->storage->InternString(
                                   decoder->description())));
            }
          } else if (event.has_stage_id()) {
            size_t stage_id = static_cast<size_t>(event.stage_id());
            if (stage_id < gpu_render_stage_ids_.size()) {
              auto description = gpu_render_stage_ids_[stage_id].second;
              if (description != kNullStringId) {
                inserter->AddArg(description_id_,
                                 Variadic::String(description));
              }
            }
          }

          if (event.render_pass_instance_id()) {
            base::StackString<512> id_str("rp:#%" PRIu64,
                                          event.render_pass_instance_id());
            inserter->AddArg(correlation_id_,
                             Variadic::String(context_->storage->InternString(
                                 id_str.string_view())));
          }

          for (auto it = event.extra_data(); it; ++it) {
            protos::pbzero::GpuRenderStageEvent_ExtraData_Decoder datum(*it);
            StringId name_id = context_->storage->InternString(datum.name());
            StringId value = context_->storage->InternString(
                datum.has_value() ? datum.value() : base::StringView());
            inserter->AddArg(name_id, Variadic::String(value));
          }

          // TODO: Create table for graphics context and lookup
          // InternedGraphicsContext.
          inserter->AddArg(
              context_id_id_,
              Variadic::Integer(static_cast<int64_t>(event.context())));
          inserter->AddArg(render_target_id_,
                           Variadic::Integer(static_cast<int64_t>(
                               event.render_target_handle())));
          inserter->AddArg(render_target_name_id_,
                           Variadic::String(render_target_name_id));
          inserter->AddArg(render_pass_id_,
                           Variadic::Integer(static_cast<int64_t>(
                               event.render_pass_handle())));
          inserter->AddArg(render_pass_name_id_,
                           Variadic::String(render_pass_name_id));
          inserter->AddArg(render_subpasses_id_,
                           Variadic::String(ParseRenderSubpasses(event)));
          inserter->AddArg(command_buffer_id_,
                           Variadic::Integer(static_cast<int64_t>(
                               event.command_buffer_handle())));
          inserter->AddArg(command_buffer_name_id_,
                           Variadic::String(command_buffer_name_id));
          inserter->AddArg(submission_id_id_,
                           Variadic::Integer(event.submission_id()));
          inserter->AddArg(
              hw_queue_id_id_,
              Variadic::Integer(static_cast<int64_t>(hw_queue_id)));
          inserter->AddArg(
              upid_id_,
              Variadic::Integer(context_->process_tracker->GetOrCreateProcess(
                  static_cast<uint32_t>(pid))));
        });
  }
}

void GpuEventParser::UpdateVulkanMemoryAllocationCounters(
    UniquePid upid,
    const VulkanMemoryEvent::Decoder& event) {
  switch (event.source()) {
    case VulkanMemoryEvent::SOURCE_DRIVER: {
      auto allocation_scope = static_cast<VulkanMemoryEvent::AllocationScope>(
          event.allocation_scope());
      if (allocation_scope == VulkanMemoryEvent::SCOPE_UNSPECIFIED) {
        return;
      }
      switch (event.operation()) {
        case VulkanMemoryEvent::OP_CREATE:
          vulkan_driver_memory_counters_[allocation_scope] +=
              event.memory_size();
          break;
        case VulkanMemoryEvent::OP_DESTROY:
          vulkan_driver_memory_counters_[allocation_scope] -=
              event.memory_size();
          break;
        case VulkanMemoryEvent::OP_UNSPECIFIED:
        case VulkanMemoryEvent::OP_BIND:
        case VulkanMemoryEvent::OP_DESTROY_BOUND:
        case VulkanMemoryEvent::OP_ANNOTATIONS:
          return;
      }
      static constexpr auto kBlueprint = tracks::CounterBlueprint(
          "vulkan_driver_mem", tracks::UnknownUnitBlueprint(),
          tracks::DimensionBlueprints(
              tracks::kProcessDimensionBlueprint,
              tracks::StringDimensionBlueprint("vulkan_allocation_scope")),
          tracks::FnNameBlueprint([](UniquePid, base::StringView scope) {
            return base::StackString<1024>("vulkan.mem.driver.scope.%.*s",
                                           int(scope.size()), scope.data());
          }));
      static constexpr std::array kEventScopes = {
          "UNSPECIFIED", "COMMAND", "OBJECT", "CACHE", "DEVICE", "INSTANCE",
      };
      TrackId track = context_->track_tracker->InternTrack(
          kBlueprint,
          tracks::Dimensions(upid, kEventScopes[uint32_t(allocation_scope)]));
      context_->event_tracker->PushCounter(
          event.timestamp(),
          static_cast<double>(vulkan_driver_memory_counters_[allocation_scope]),
          track);
      break;
    }
    case VulkanMemoryEvent::SOURCE_DEVICE_MEMORY: {
      auto memory_type = static_cast<uint32_t>(event.memory_type());
      switch (event.operation()) {
        case VulkanMemoryEvent::OP_CREATE:
          vulkan_device_memory_counters_allocate_[memory_type] +=
              event.memory_size();
          break;
        case VulkanMemoryEvent::OP_DESTROY:
          vulkan_device_memory_counters_allocate_[memory_type] -=
              event.memory_size();
          break;
        case VulkanMemoryEvent::OP_UNSPECIFIED:
        case VulkanMemoryEvent::OP_BIND:
        case VulkanMemoryEvent::OP_DESTROY_BOUND:
        case VulkanMemoryEvent::OP_ANNOTATIONS:
          return;
      }
      static constexpr auto kBlueprint = tracks::CounterBlueprint(
          "vulkan_device_mem_allocation", tracks::UnknownUnitBlueprint(),
          tracks::DimensionBlueprints(
              tracks::kProcessDimensionBlueprint,
              tracks::UintDimensionBlueprint("vulkan_memory_type")),
          tracks::FnNameBlueprint([](UniquePid, uint32_t type) {
            return base::StackString<1024>(
                "vulkan.mem.device.memory.type.%u.allocation", type);
          }));
      TrackId track = context_->track_tracker->InternTrack(
          kBlueprint, tracks::Dimensions(upid, memory_type));
      context_->event_tracker->PushCounter(
          event.timestamp(),
          static_cast<double>(
              vulkan_device_memory_counters_allocate_[memory_type]),
          track);
      break;
    }
    case VulkanMemoryEvent::SOURCE_BUFFER:
    case VulkanMemoryEvent::SOURCE_IMAGE: {
      auto memory_type = static_cast<uint32_t>(event.memory_type());
      switch (event.operation()) {
        case VulkanMemoryEvent::OP_BIND:
          vulkan_device_memory_counters_bind_[memory_type] +=
              event.memory_size();
          break;
        case VulkanMemoryEvent::OP_DESTROY_BOUND:
          vulkan_device_memory_counters_bind_[memory_type] -=
              event.memory_size();
          break;
        case VulkanMemoryEvent::OP_UNSPECIFIED:
        case VulkanMemoryEvent::OP_CREATE:
        case VulkanMemoryEvent::OP_DESTROY:
        case VulkanMemoryEvent::OP_ANNOTATIONS:
          return;
      }
      static constexpr auto kBlueprint = tracks::CounterBlueprint(
          "vulkan_device_mem_bind", tracks::UnknownUnitBlueprint(),
          tracks::DimensionBlueprints(
              tracks::kProcessDimensionBlueprint,
              tracks::UintDimensionBlueprint("vulkan_memory_type")),
          tracks::FnNameBlueprint([](UniquePid, uint32_t type) {
            return base::StackString<1024>(
                "vulkan.mem.device.memory.type.%u.bind", type);
          }));
      TrackId track = context_->track_tracker->InternTrack(
          kBlueprint, tracks::Dimensions(upid, memory_type));
      context_->event_tracker->PushCounter(
          event.timestamp(),
          static_cast<double>(vulkan_device_memory_counters_bind_[memory_type]),
          track);
      break;
    }
    case VulkanMemoryEvent::SOURCE_UNSPECIFIED:
    case VulkanMemoryEvent::SOURCE_DEVICE:
      return;
  }
}

void GpuEventParser::ParseVulkanMemoryEvent(
    PacketSequenceStateGeneration* sequence_state,
    ConstBytes blob) {
  using protos::pbzero::InternedData;
  VulkanMemoryEvent::Decoder vulkan_memory_event(blob);
  tables::VulkanMemoryAllocationsTable::Row vulkan_memory_event_row;
  vulkan_memory_event_row.source = vulkan_memory_tracker_.FindSourceString(
      static_cast<VulkanMemoryEvent::Source>(vulkan_memory_event.source()));
  vulkan_memory_event_row.operation =
      vulkan_memory_tracker_.FindOperationString(
          static_cast<VulkanMemoryEvent::Operation>(
              vulkan_memory_event.operation()));
  vulkan_memory_event_row.timestamp = vulkan_memory_event.timestamp();
  vulkan_memory_event_row.upid =
      context_->process_tracker->GetOrCreateProcess(vulkan_memory_event.pid());
  if (vulkan_memory_event.has_device()) {
    vulkan_memory_event_row.device =
        static_cast<int64_t>(vulkan_memory_event.device());
  }
  if (vulkan_memory_event.has_device_memory()) {
    vulkan_memory_event_row.device_memory =
        static_cast<int64_t>(vulkan_memory_event.device_memory());
  }
  if (vulkan_memory_event.has_heap()) {
    vulkan_memory_event_row.heap = vulkan_memory_event.heap();
  }
  if (vulkan_memory_event.has_memory_type()) {
    vulkan_memory_event_row.memory_type = vulkan_memory_event.memory_type();
  }
  if (vulkan_memory_event.has_caller_iid()) {
    vulkan_memory_event_row.function_name =
        vulkan_memory_tracker_
            .GetInternedString<InternedData::kFunctionNamesFieldNumber>(
                sequence_state,
                static_cast<uint64_t>(vulkan_memory_event.caller_iid()));
  }
  if (vulkan_memory_event.has_object_handle()) {
    vulkan_memory_event_row.object_handle =
        static_cast<int64_t>(vulkan_memory_event.object_handle());
  }
  if (vulkan_memory_event.has_memory_address()) {
    vulkan_memory_event_row.memory_address =
        static_cast<int64_t>(vulkan_memory_event.memory_address());
  }
  if (vulkan_memory_event.has_memory_size()) {
    vulkan_memory_event_row.memory_size =
        static_cast<int64_t>(vulkan_memory_event.memory_size());
  }
  if (vulkan_memory_event.has_allocation_scope()) {
    vulkan_memory_event_row.scope =
        vulkan_memory_tracker_.FindAllocationScopeString(
            static_cast<VulkanMemoryEvent::AllocationScope>(
                vulkan_memory_event.allocation_scope()));
  }

  UpdateVulkanMemoryAllocationCounters(vulkan_memory_event_row.upid.value(),
                                       vulkan_memory_event);

  auto* allocs = context_->storage->mutable_vulkan_memory_allocations_table();
  VulkanAllocId id = allocs->Insert(vulkan_memory_event_row).id;

  if (vulkan_memory_event.has_annotations()) {
    ArgsTracker args_tracker(context_);
    auto inserter = args_tracker.AddArgsTo(id);

    for (auto it = vulkan_memory_event.annotations(); it; ++it) {
      protos::pbzero::VulkanMemoryEventAnnotation::Decoder annotation(*it);

      auto key_id =
          vulkan_memory_tracker_
              .GetInternedString<InternedData::kVulkanMemoryKeysFieldNumber>(
                  sequence_state, static_cast<uint64_t>(annotation.key_iid()));

      if (annotation.has_int_value()) {
        inserter.AddArg(key_id, Variadic::Integer(annotation.int_value()));
      } else if (annotation.has_double_value()) {
        inserter.AddArg(key_id, Variadic::Real(annotation.double_value()));
      } else if (annotation.has_string_iid()) {
        auto string_id =
            vulkan_memory_tracker_
                .GetInternedString<InternedData::kVulkanMemoryKeysFieldNumber>(
                    sequence_state,
                    static_cast<uint64_t>(annotation.string_iid()));

        inserter.AddArg(key_id, Variadic::String(string_id));
      }
    }
  }
}

void GpuEventParser::ParseGpuLog(int64_t ts, ConstBytes blob) {
  protos::pbzero::GpuLog::Decoder event(blob);

  static constexpr auto kGpuLogBlueprint =
      tracks::SliceBlueprint("gpu_log", tracks::DimensionBlueprints(),
                             tracks::StaticNameBlueprint("GPU Log"));
  TrackId track_id = context_->track_tracker->InternTrack(kGpuLogBlueprint);
  auto severity = static_cast<size_t>(event.severity());
  StringId severity_id =
      severity < log_severity_ids_.size()
          ? log_severity_ids_[static_cast<size_t>(event.severity())]
          : log_severity_ids_[log_severity_ids_.size() - 1];
  context_->slice_tracker->Scoped(
      ts, track_id, kNullStringId, severity_id, 0,
      [this, &event](ArgsTracker::BoundInserter* inserter) {
        if (event.has_tag()) {
          inserter->AddArg(
              tag_id_,
              Variadic::String(context_->storage->InternString(event.tag())));
        }
        if (event.has_log_message()) {
          inserter->AddArg(log_message_id_,
                           Variadic::String(context_->storage->InternString(
                               event.log_message())));
        }
      });
}

void GpuEventParser::ParseVulkanApiEvent(int64_t ts, ConstBytes blob) {
  protos::pbzero::VulkanApiEvent::Decoder vk_event(blob);
  if (vk_event.has_vk_debug_utils_object_name()) {
    protos::pbzero::VulkanApiEvent_VkDebugUtilsObjectName::Decoder event(
        vk_event.vk_debug_utils_object_name());
    debug_marker_names_[event.object_type()][event.object()] =
        event.object_name().ToStdString();
  }
  if (!vk_event.has_vk_queue_submit()) {
    return;
  }
  protos::pbzero::VulkanApiEvent_VkQueueSubmit::Decoder event(
      vk_event.vk_queue_submit());
  // Once flow table is implemented, we can create a nice UI that link the
  // vkQueueSubmit to GpuRenderStageEvent.  For now, just add it as in a GPU
  // track so that they can appear close to the render stage slices.
  static constexpr auto kVulkanEventsBlueprint =
      tracks::SliceBlueprint("vulkan_events", tracks::DimensionBlueprints(),
                             tracks::StaticNameBlueprint("Vulkan Events"));
  TrackId track_id =
      context_->track_tracker->InternTrack(kVulkanEventsBlueprint);
  context_->slice_tracker->Scoped(
      ts, track_id, kNullStringId, vk_queue_submit_id_,
      static_cast<int64_t>(event.duration_ns()),
      [this, &event](ArgsTracker::BoundInserter* inserter) {
        inserter->AddArg(pid_id_, Variadic::Integer(event.pid()));
        inserter->AddArg(tid_id_, Variadic::Integer(event.tid()));
        if (event.has_vk_command_buffers()) {
          inserter->AddArg(command_buffer_id_,
                           Variadic::Integer(static_cast<int64_t>(
                               *event.vk_command_buffers())));
        }
        inserter->AddArg(submission_id_id_,
                         Variadic::Integer(event.submission_id()));
      });
}

void GpuEventParser::ParseGpuMemTotalEvent(int64_t ts, ConstBytes blob) {
  protos::pbzero::GpuMemTotalEvent::Decoder gpu_mem_total(blob);

  TrackId track = kInvalidTrackId;
  const uint32_t pid = gpu_mem_total.pid();
  if (pid == 0) {
    // Pid 0 is used to indicate the global total
    track =
        context_->track_tracker->InternTrack(tracks::kGlobalGpuMemoryBlueprint);
  } else {
    // Process emitting the packet can be different from the pid in the event.
    UniqueTid utid = context_->process_tracker->UpdateThread(pid, pid);
    UniquePid upid = context_->storage->thread_table()[utid].upid().value_or(0);
    track = context_->track_tracker->InternTrack(
        tracks::kProcessGpuMemoryBlueprint, tracks::Dimensions(upid));
  }
  context_->event_tracker->PushCounter(
      ts, static_cast<double>(gpu_mem_total.size()), track);
}

}  // namespace perfetto::trace_processor
