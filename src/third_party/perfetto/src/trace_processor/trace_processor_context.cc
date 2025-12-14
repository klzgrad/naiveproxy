/*
 * Copyright (C) 2018 The Android Open Source Project
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

#include "src/trace_processor/types/trace_processor_context.h"

#include <cstdint>
#include <memory>
#include <optional>
#include <utility>

#include "perfetto/base/logging.h"
#include "perfetto/trace_processor/basic_types.h"
#include "src/trace_processor/forwarding_trace_parser.h"
#include "src/trace_processor/importers/common/args_translation_table.h"
#include "src/trace_processor/importers/common/clock_converter.h"
#include "src/trace_processor/importers/common/clock_tracker.h"
#include "src/trace_processor/importers/common/cpu_tracker.h"
#include "src/trace_processor/importers/common/event_tracker.h"
#include "src/trace_processor/importers/common/flow_tracker.h"
#include "src/trace_processor/importers/common/import_logs_tracker.h"
#include "src/trace_processor/importers/common/machine_tracker.h"
#include "src/trace_processor/importers/common/mapping_tracker.h"
#include "src/trace_processor/importers/common/metadata_tracker.h"
#include "src/trace_processor/importers/common/process_track_translation_table.h"
#include "src/trace_processor/importers/common/process_tracker.h"
#include "src/trace_processor/importers/common/registered_file_tracker.h"
#include "src/trace_processor/importers/common/sched_event_tracker.h"
#include "src/trace_processor/importers/common/slice_tracker.h"
#include "src/trace_processor/importers/common/stack_profile_tracker.h"
#include "src/trace_processor/importers/common/symbol_tracker.h"
#include "src/trace_processor/importers/common/trace_file_tracker.h"
#include "src/trace_processor/importers/common/track_compressor.h"
#include "src/trace_processor/importers/common/track_tracker.h"
#include "src/trace_processor/importers/proto/proto_importer_module.h"
#include "src/trace_processor/importers/proto/proto_trace_reader.h"
#include "src/trace_processor/storage/trace_storage.h"
#include "src/trace_processor/trace_reader_registry.h"
#include "src/trace_processor/types/trace_processor_context_ptr.h"

namespace perfetto::trace_processor {
namespace {

template <typename T>
using Ptr = TraceProcessorContextPtr<T>;

void InitPerTraceAndMachineState(TraceProcessorContext* context) {
  // Per-machine state (legacy).
  context->track_tracker = Ptr<TrackTracker>::MakeRoot(context);
  context->track_compressor = Ptr<TrackCompressor>::MakeRoot(context);
  context->slice_tracker = Ptr<SliceTracker>::MakeRoot(context);
  context->slice_translation_table =
      Ptr<SliceTranslationTable>::MakeRoot(context->storage.get());
  context->flow_tracker = Ptr<FlowTracker>::MakeRoot(context);
  context->process_track_translation_table =
      Ptr<ProcessTrackTranslationTable>::MakeRoot(context->storage.get());
  context->event_tracker = Ptr<EventTracker>::MakeRoot(context);
  context->sched_event_tracker = Ptr<SchedEventTracker>::MakeRoot(context);
  context->args_translation_table =
      Ptr<ArgsTranslationTable>::MakeRoot(context->storage.get());

  context->slice_tracker->SetOnSliceBeginCallback(
      [context](TrackId track_id, SliceId slice_id) {
        context->flow_tracker->ClosePendingEventsOnTrack(track_id, slice_id);
      });
}

void InitPerMachineState(TraceProcessorContext* context, uint32_t machine_id) {
  context->symbol_tracker = Ptr<SymbolTracker>::MakeRoot(context);
  context->machine_tracker = Ptr<MachineTracker>::MakeRoot(context, machine_id);
  context->process_tracker = Ptr<ProcessTracker>::MakeRoot(context);
  std::unique_ptr<ClockSynchronizerListenerImpl> clock_tracker_listener =
      std::make_unique<ClockSynchronizerListenerImpl>(context);
  context->clock_tracker =
      Ptr<ClockTracker>::MakeRoot(std::move(clock_tracker_listener));
  context->mapping_tracker = Ptr<MappingTracker>::MakeRoot(context);
  context->cpu_tracker = Ptr<CpuTracker>::MakeRoot(context);
}

void CopyPerMachineState(const TraceProcessorContext* source,
                         TraceProcessorContext* dest) {
  dest->symbol_tracker = source->symbol_tracker.Fork();
  dest->machine_tracker = source->machine_tracker.Fork();
  dest->process_tracker = source->process_tracker.Fork();
  dest->clock_tracker = source->clock_tracker.Fork();
  dest->mapping_tracker = source->mapping_tracker.Fork();
  dest->cpu_tracker = source->cpu_tracker.Fork();
}

void InitPerTraceState(TraceProcessorContext* context, uint32_t raw_trace_id) {
  context->trace_state = Ptr<TraceProcessorContext::TraceState>::MakeRoot(
      TraceProcessorContext::TraceState{raw_trace_id});
  context->content_analyzer = nullptr;
  context->import_logs_tracker =
      Ptr<ImportLogsTracker>::MakeRoot(context, raw_trace_id);
}

void CopyTraceState(const TraceProcessorContext* source,
                    TraceProcessorContext* dest) {
  dest->trace_state = source->trace_state.Fork();
  dest->content_analyzer = source->content_analyzer.Fork();
  dest->import_logs_tracker = source->import_logs_tracker.Fork();
}

Ptr<TraceSorter> CreateSorter(TraceProcessorContext* context,
                              const Config& config) {
  TraceSorter::EventHandling event_handling;
  switch (config.parsing_mode) {
    case ParsingMode::kDefault:
      event_handling = TraceSorter::EventHandling::kSortAndPush;
      break;
    case ParsingMode::kTokenizeOnly:
      event_handling = TraceSorter::EventHandling::kDrop;
      break;
    case ParsingMode::kTokenizeAndSort:
      event_handling = TraceSorter::EventHandling::kSortAndDrop;
      break;
  }
  if (config.enable_dev_features) {
    auto it = config.dev_flags.find("drop-after-sort");
    if (it != config.dev_flags.end() && it->second == "true") {
      event_handling = TraceSorter::EventHandling::kSortAndDrop;
    }
  }
  return Ptr<TraceSorter>::MakeRoot(context, TraceSorter::SortingMode::kDefault,
                                    event_handling);
}

void InitGlobalState(TraceProcessorContext* context, const Config& config) {
  // Global state.
  context->config = config;
  context->storage = Ptr<TraceStorage>::MakeRoot(config);
  context->sorter = CreateSorter(context, config);
  context->reader_registry = Ptr<TraceReaderRegistry>::MakeRoot();
  context->global_args_tracker =
      Ptr<GlobalArgsTracker>::MakeRoot(context->storage.get());
  context->trace_file_tracker = Ptr<TraceFileTracker>::MakeRoot(context);
  context->descriptor_pool_ = Ptr<DescriptorPool>::MakeRoot();
  context->forked_context_state =
      Ptr<TraceProcessorContext::ForkedContextState>::MakeRoot();
  context->clock_converter = Ptr<ClockConverter>::MakeRoot(context);
  context->track_group_idx_state =
      Ptr<TrackCompressorGroupIdxState>::MakeRoot();
  context->stack_profile_tracker = Ptr<StackProfileTracker>::MakeRoot(context);
  context->deobfuscation_tracker = nullptr;
  context->register_additional_proto_modules = nullptr;

  // Per-Trace State (Miscategorized).
  context->metadata_tracker =
      Ptr<MetadataTracker>::MakeRoot(context->storage.get());
  context->registered_file_tracker =
      Ptr<RegisteredFileTracker>::MakeRoot(context);
  context->uuid_state = Ptr<TraceProcessorContext::UuidState>::MakeRoot();
  context->heap_graph_tracker = nullptr;
}

void CopyGlobalState(const TraceProcessorContext* source,
                     TraceProcessorContext* dest) {
  // Global state.
  dest->config = source->config;
  dest->storage = source->storage.Fork();
  dest->sorter = source->sorter.Fork();
  dest->reader_registry = source->reader_registry.Fork();
  dest->global_args_tracker = source->global_args_tracker.Fork();
  dest->trace_file_tracker = source->trace_file_tracker.Fork();
  dest->descriptor_pool_ = source->descriptor_pool_.Fork();
  dest->forked_context_state = source->forked_context_state.Fork();
  dest->clock_converter = source->clock_converter.Fork();
  dest->track_group_idx_state = source->track_group_idx_state.Fork();
  dest->register_additional_proto_modules =
      source->register_additional_proto_modules;

  // Per-Trace State (Miscategorized).
  dest->metadata_tracker = source->metadata_tracker.Fork();
  dest->registered_file_tracker = source->registered_file_tracker.Fork();
  dest->uuid_state = source->uuid_state.Fork();
  dest->heap_graph_tracker = source->heap_graph_tracker.Fork();
  dest->deobfuscation_tracker = source->deobfuscation_tracker.Fork();
  dest->stack_profile_tracker = source->stack_profile_tracker.Fork();
}

}  // namespace

TraceProcessorContext::TraceProcessorContext() = default;
TraceProcessorContext::TraceProcessorContext(const Config& _config) {
  InitGlobalState(this, _config);
}
TraceProcessorContext::~TraceProcessorContext() = default;

TraceProcessorContext* TraceProcessorContext::ForkContextForTrace(
    uint32_t raw_trace_id,
    uint32_t default_raw_machine_id) const {
  auto [it, inserted] =
      forked_context_state->trace_and_machine_to_context.Insert(
          std::pair(raw_trace_id, default_raw_machine_id), nullptr);
  if (inserted) {
    auto context = std::make_unique<TraceProcessorContext>();
    CopyGlobalState(this, context.get());

    // Initialize per-trace state.
    auto [trace_it, trace_inserted] =
        forked_context_state->trace_to_context.Insert(raw_trace_id,
                                                      context.get());
    if (trace_inserted) {
      InitPerTraceState(context.get(), raw_trace_id);
    } else {
      CopyTraceState(*trace_it, context.get());
    }

    // Initialize per-machine state.
    auto [machine_it, machine_inserted] =
        forked_context_state->machine_to_context.Insert(default_raw_machine_id,
                                                        context.get());
    if (machine_inserted) {
      InitPerMachineState(context.get(), default_raw_machine_id);
    } else {
      CopyPerMachineState(*machine_it, context.get());
    }

    // Initialize per-trace & per-machine state.
    InitPerTraceAndMachineState(context.get());

    *it = std::move(context);
  }
  return it->get();
}

TraceProcessorContext*
TraceProcessorContext::ForkContextForMachineInCurrentTrace(
    uint32_t raw_machine_id) const {
  PERFETTO_CHECK(trace_state);
  return ForkContextForTrace(trace_state->raw_trace_id, raw_machine_id);
}

std::optional<MachineId> TraceProcessorContext::machine_id() const {
  if (!machine_tracker) {
    // Doesn't require that |machine_tracker| is initialized, e.g. in unit
    // tests.
    return std::nullopt;
  }
  return machine_tracker->machine_id();
}

void TraceProcessorContext::DestroyParsingState() {
  auto _storage = std::move(storage);

  // TODO(b/309623584): Decouple from storage and remove from here. This
  // function should only move storage and delete everything else.
  auto _heap_graph_tracker = std::move(heap_graph_tracker);
  auto _clock_converter = std::move(clock_converter);
  // "to_ftrace" textual converter of the "raw" table requires remembering the
  // kernel version (inside system_info_tracker) to know how to textualise
  // sched_switch.prev_state bitflags.
  auto _system_info_tracker = std::move(system_info_tracker);

  // "__intrinsic_winscope_proto_to_args_with_defaults" and trace summarization
  // both require the descriptor pool to be alive.
  auto _descriptor_pool_ = std::move(descriptor_pool_);

  this->~TraceProcessorContext();
  new (this) TraceProcessorContext();

  storage = std::move(_storage);
  heap_graph_tracker = std::move(_heap_graph_tracker);
  clock_converter = std::move(_clock_converter);
  system_info_tracker = std::move(_system_info_tracker);
  descriptor_pool_ = std::move(_descriptor_pool_);
}

}  // namespace perfetto::trace_processor
