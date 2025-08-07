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

#include <memory>
#include <optional>

#include "src/trace_processor/forwarding_trace_parser.h"
#include "src/trace_processor/importers/common/args_tracker.h"
#include "src/trace_processor/importers/common/args_translation_table.h"
#include "src/trace_processor/importers/common/clock_converter.h"
#include "src/trace_processor/importers/common/clock_tracker.h"
#include "src/trace_processor/importers/common/cpu_tracker.h"
#include "src/trace_processor/importers/common/event_tracker.h"
#include "src/trace_processor/importers/common/flow_tracker.h"
#include "src/trace_processor/importers/common/global_args_tracker.h"
#include "src/trace_processor/importers/common/legacy_v8_cpu_profile_tracker.h"
#include "src/trace_processor/importers/common/machine_tracker.h"
#include "src/trace_processor/importers/common/mapping_tracker.h"
#include "src/trace_processor/importers/common/metadata_tracker.h"
#include "src/trace_processor/importers/common/process_track_translation_table.h"
#include "src/trace_processor/importers/common/process_tracker.h"
#include "src/trace_processor/importers/common/sched_event_tracker.h"
#include "src/trace_processor/importers/common/slice_tracker.h"
#include "src/trace_processor/importers/common/slice_translation_table.h"
#include "src/trace_processor/importers/common/stack_profile_tracker.h"
#include "src/trace_processor/importers/common/trace_file_tracker.h"
#include "src/trace_processor/importers/common/track_compressor.h"
#include "src/trace_processor/importers/common/track_tracker.h"
#include "src/trace_processor/importers/proto/multi_machine_trace_manager.h"
#include "src/trace_processor/importers/proto/perf_sample_tracker.h"
#include "src/trace_processor/importers/proto/proto_importer_module.h"
#include "src/trace_processor/storage/trace_storage.h"
#include "src/trace_processor/trace_reader_registry.h"

namespace perfetto::trace_processor {

TraceProcessorContext::TraceProcessorContext(const InitArgs& args)
    : config(args.config), storage(args.storage) {
  reader_registry = std::make_unique<TraceReaderRegistry>(this);
  // Init the trackers.
  machine_tracker = std::make_unique<MachineTracker>(this, args.raw_machine_id);
  if (!machine_id()) {
    multi_machine_trace_manager =
        std::make_unique<MultiMachineTraceManager>(this);
  }
  track_tracker = std::make_unique<TrackTracker>(this);
  track_compressor = std::make_unique<TrackCompressor>(this);
  args_tracker = std::make_unique<ArgsTracker>(this);
  args_translation_table =
      std::make_unique<ArgsTranslationTable>(storage.get());
  slice_tracker = std::make_unique<SliceTracker>(this);
  slice_translation_table =
      std::make_unique<SliceTranslationTable>(storage.get());
  flow_tracker = std::make_unique<FlowTracker>(this);
  event_tracker = std::make_unique<EventTracker>(this);
  sched_event_tracker = std::make_unique<SchedEventTracker>(this);
  process_tracker = std::make_unique<ProcessTracker>(this);
  process_track_translation_table =
      std::make_unique<ProcessTrackTranslationTable>(storage.get());
  clock_tracker = std::make_unique<ClockTracker>(this);
  clock_converter = std::make_unique<ClockConverter>(this);
  mapping_tracker = std::make_unique<MappingTracker>(this);
  perf_sample_tracker = std::make_unique<PerfSampleTracker>(this);
  stack_profile_tracker = std::make_unique<StackProfileTracker>(this);
  metadata_tracker = std::make_unique<MetadataTracker>(storage.get());
  cpu_tracker = std::make_unique<CpuTracker>(this);
  global_args_tracker = std::make_shared<GlobalArgsTracker>(storage.get());
  descriptor_pool_ = std::make_unique<DescriptorPool>();

  slice_tracker->SetOnSliceBeginCallback(
      [this](TrackId track_id, SliceId slice_id) {
        flow_tracker->ClosePendingEventsOnTrack(track_id, slice_id);
      });

  trace_file_tracker = std::make_unique<TraceFileTracker>(this);
  legacy_v8_cpu_profile_tracker =
      std::make_unique<LegacyV8CpuProfileTracker>(this);
}

TraceProcessorContext::TraceProcessorContext() = default;
TraceProcessorContext::~TraceProcessorContext() = default;

TraceProcessorContext::TraceProcessorContext(TraceProcessorContext&&) = default;
TraceProcessorContext& TraceProcessorContext::operator=(
    TraceProcessorContext&&) = default;

std::optional<MachineId> TraceProcessorContext::machine_id() const {
  if (!machine_tracker) {
    // Doesn't require that |machine_tracker| is initialized, e.g. in unit
    // tests.
    return std::nullopt;
  }
  return machine_tracker->machine_id();
}

}  // namespace perfetto::trace_processor
