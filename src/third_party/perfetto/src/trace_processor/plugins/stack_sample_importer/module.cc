/*
 * Copyright (C) 2026 The Android Open Source Project
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

#include "src/trace_processor/plugins/stack_sample_importer/module.h"

#include <algorithm>
#include <cstdint>
#include <optional>
#include <vector>

#include "perfetto/ext/base/string_view.h"
#include "src/trace_processor/importers/common/args_tracker.h"
#include "src/trace_processor/importers/common/cpu_tracker.h"
#include "src/trace_processor/importers/common/event_tracker.h"
#include "src/trace_processor/importers/common/mapping_tracker.h"
#include "src/trace_processor/importers/common/process_tracker.h"
#include "src/trace_processor/importers/common/profiler_sample_tracker.h"
#include "src/trace_processor/importers/common/stack_profile_tracker.h"
#include "src/trace_processor/importers/common/track_tracker.h"
#include "src/trace_processor/importers/common/tracks.h"
#include "src/trace_processor/importers/common/virtual_memory_mapping.h"
#include "src/trace_processor/importers/proto/packet_sequence_state_generation.h"
#include "src/trace_processor/importers/proto/stack_profile_sequence_state.h"
#include "src/trace_processor/storage/trace_storage.h"
#include "src/trace_processor/tables/profiler_tables_py.h"
#include "src/trace_processor/types/trace_processor_context.h"
#include "src/trace_processor/types/variadic.h"
#include "src/trace_processor/util/interned_message_view.h"

#include "protos/perfetto/trace/interned_data/interned_data.pbzero.h"
#include "protos/perfetto/trace/profiling/inline_callstack.pbzero.h"
#include "protos/perfetto/trace/profiling/stack_sample.pbzero.h"
#include "protos/perfetto/trace/trace_packet.pbzero.h"
#include "protos/perfetto/trace/trace_packet_defaults.pbzero.h"

namespace perfetto::trace_processor::stack_sample_importer {
namespace {

using perfetto::protos::pbzero::TracePacket;

// Counter tracks model counter instances (see CounterDescriptor.Scope),
// keeping each profiler session's streams separate. SCOPE_GLOBAL counters
// get one track per profiler session; SCOPE_CPU counters one track per
// (session, cpu).
constexpr auto kStackSampleSessionCounterBlueprint = tracks::CounterBlueprint(
    "stack_sample_session_counter",
    tracks::DynamicUnitBlueprint(),
    tracks::DimensionBlueprints(
        tracks::UintDimensionBlueprint("session_id"),
        tracks::StringDimensionBlueprint("source"),
        tracks::StringDimensionBlueprint("counter_name")),
    tracks::DynamicNameBlueprint());

constexpr auto kStackSampleCpuCounterBlueprint = tracks::CounterBlueprint(
    "stack_sample_cpu_counter",
    tracks::DynamicUnitBlueprint(),
    tracks::DimensionBlueprints(
        tracks::kCpuDimensionBlueprint,
        tracks::UintDimensionBlueprint("session_id"),
        tracks::StringDimensionBlueprint("source"),
        tracks::StringDimensionBlueprint("counter_name")),
    tracks::DynamicNameBlueprint());

const char* StringifyStackSampleMode(protos::pbzero::StackSample::Mode mode) {
  using StackSample = protos::pbzero::StackSample;
  switch (mode) {
    case StackSample::Mode::MODE_UNKNOWN:
      return nullptr;
    case StackSample::Mode::MODE_USER:
      return "user";
    case StackSample::Mode::MODE_KERNEL:
      return "kernel";
    case StackSample::Mode::MODE_HYPERVISOR:
      return "hypervisor";
    case StackSample::Mode::MODE_GUEST_USER:
      return "guest_user";
    case StackSample::Mode::MODE_GUEST_KERNEL:
      return "guest_kernel";
  }
  return nullptr;
}

const char* StringifyProfileUnit(protos::pbzero::StackSample::Unit unit) {
  using StackSample = protos::pbzero::StackSample;
  switch (unit) {
    case StackSample::Unit::UNIT_UNSPECIFIED:
      return "";
    case StackSample::Unit::UNIT_NANOSECONDS:
      return "ns";
    case StackSample::Unit::UNIT_CPU_CYCLES:
      return "cycles";
    case StackSample::Unit::UNIT_INSTRUCTIONS:
      return "instructions";
    case StackSample::Unit::UNIT_BYTES:
      return "bytes";
    case StackSample::Unit::UNIT_PAGE_FAULTS:
      return "page-faults";
    case StackSample::Unit::UNIT_CACHE_MISSES:
      return "cache-misses";
    case StackSample::Unit::UNIT_CACHE_REFERENCES:
      return "cache-references";
    case StackSample::Unit::UNIT_BRANCH_MISSES:
      return "branch-misses";
    case StackSample::Unit::UNIT_COUNT:
      return "count";
  }
  return "";
}

}  // namespace

StackSampleModule::StackSampleModule(ProtoImporterModuleContext* module_context,
                                     TraceProcessorContext* context)
    : ProtoImporterModule(module_context), context_(context) {
  RegisterForField(TracePacket::kStackSampleFieldNumber);
}

void StackSampleModule::ParseField(const ParseFieldArgs& args) {
  if (args.field.id() == TracePacket::kStackSampleFieldNumber) {
    ParseStackSample(args.ts, args.decoder.trusted_packet_sequence_id(),
                     args.data.sequence_state.get(),
                     args.field.Cast<TracePacket::kStackSample>());
  }
}

tables::ProfilerSessionTable::Id StackSampleModule::GetOrCreateSession(
    uint32_t seq_id,
    StringId source) {
  if (auto* id = sessions_.Find(seq_id)) {
    return *id;
  }
  tables::ProfilerSessionTable::Row row;
  row.source = source;
  auto id = context_->storage->mutable_profiler_session_table()->Insert(row).id;
  sessions_.Insert(seq_id, id);
  return id;
}

std::optional<TrackId> StackSampleModule::InternCounterTrack(
    tables::ProfilerSessionTable::Id session_id,
    StringId source,
    const protos::pbzero::StackSample::CounterDescriptor::Decoder& desc,
    bool is_timebase,
    std::optional<uint32_t> cpu) {
  using StackSample = protos::pbzero::StackSample;
  using CounterDescriptor = StackSample::CounterDescriptor;
  TraceStorage* storage = context_->storage.get();

  StringId name_id = storage->InternString(desc.name());
  StringId unit_id = kNullStringId;
  if (desc.has_unit() && desc.unit() != StackSample::Unit::UNIT_UNSPECIFIED) {
    unit_id = storage->InternString(
        StringifyProfileUnit(static_cast<StackSample::Unit>(desc.unit())));
  } else if (desc.unit_str().size > 0) {
    unit_id = storage->InternString(desc.unit_str());
  }

  // The timebase unit says what quantity the profiler sampled on; record it
  // on the session.
  if (is_timebase && unit_id != kNullStringId) {
    auto session_row = (*storage->mutable_profiler_session_table())[session_id];
    if (!session_row.timebase_unit().has_value()) {
      session_row.set_timebase_unit(unit_id);
    }
  }

  auto args_fn = [&, this](ArgsTracker::BoundInserter& inserter) {
    inserter.AddArg(context_->storage->InternString("is_timebase"),
                    Variadic::Boolean(is_timebase));
    if (desc.has_unit_multiplier()) {
      inserter.AddArg(
          context_->storage->InternString("unit_multiplier"),
          Variadic::Integer(static_cast<int64_t>(desc.unit_multiplier())));
    }
    if (desc.description().size > 0) {
      inserter.AddArg(context_->storage->InternString("description"),
                      Variadic::String(
                          context_->storage->InternString(desc.description())));
    }
  };

  auto scope = desc.has_scope()
                   ? static_cast<CounterDescriptor::Scope>(desc.scope())
                   : CounterDescriptor::Scope::SCOPE_GLOBAL;
  if (scope == CounterDescriptor::Scope::SCOPE_CPU) {
    // Weights of a per-cpu counter instance cannot be attributed without
    // knowing which cpu the sample was taken on.
    if (!cpu) {
      return std::nullopt;
    }
    return context_->track_tracker->InternTrack(
        kStackSampleCpuCounterBlueprint,
        tracks::Dimensions(*cpu, session_id.value, storage->GetString(source),
                           storage->GetString(name_id)),
        tracks::DynamicName(name_id), args_fn, tracks::DynamicUnit(unit_id));
  }
  return context_->track_tracker->InternTrack(
      kStackSampleSessionCounterBlueprint,
      tracks::Dimensions(session_id.value, storage->GetString(source),
                         storage->GetString(name_id)),
      tracks::DynamicName(name_id), args_fn, tracks::DynamicUnit(unit_id));
}

// Resolves a callstack that is either interned (callstack_iid) or fully
// inline (an InlineCallstack whose frames carry function names and source
// locations directly). Inline frames are interned into a dummy mapping, like
// TrackEvent inline callstacks.
std::optional<CallsiteId> StackSampleModule::ResolveCallstack(
    PacketSequenceStateGeneration* sequence_state,
    std::optional<UniquePid> upid,
    const protos::pbzero::StackSample::Decoder& sample) {
  if (sample.has_callstack_iid()) {
    auto* state = sequence_state->GetCustomState<StackProfileSequenceState>();
    return state->FindOrInsertCallstack(sequence_state, upid,
                                        sample.callstack_iid());
  }
  if (!sample.has_callstack()) {
    return std::nullopt;
  }
  if (!inline_callstack_mapping_) {
    inline_callstack_mapping_ =
        &context_->mapping_tracker->CreateDummyMapping("stack_sample_inline");
  }
  protos::pbzero::InlineCallstack::Decoder callstack(sample.callstack());
  std::optional<CallsiteId> callsite_id;
  uint32_t depth = 0;
  for (auto it = callstack.frames(); it; ++it, ++depth) {
    protos::pbzero::InlineCallstack::Frame::Decoder frame(*it);
    std::optional<base::StringView> source_file;
    if (frame.has_source_file()) {
      source_file = frame.source_file();
    }
    std::optional<uint32_t> line_number;
    if (frame.has_line_number()) {
      line_number = frame.line_number();
    }
    FrameId frame_id = inline_callstack_mapping_->InternDummyFrame(
        frame.function_name(), source_file, line_number);
    callsite_id = context_->stack_profile_tracker->InternCallsite(
        callsite_id, frame_id, depth);
  }
  return callsite_id;
}

std::vector<CounterId> StackSampleModule::ParseCounterValues(
    int64_t ts,
    PacketSequenceStateGeneration* sequence_state,
    tables::ProfilerSessionTable::Id session_id,
    StringId source,
    std::optional<uint32_t> cpu,
    const protos::pbzero::StackSample::Decoder& sample,
    const std::optional<protos::pbzero::StackSampleDefaults::Decoder>&
        defaults) {
  using protos::pbzero::InternedData;
  using CounterDescriptor = protos::pbzero::StackSample::CounterDescriptor;

  std::vector<CounterId> counter_ids;

  auto intern_track = [&](const CounterDescriptor::Decoder& desc,
                          bool is_timebase) {
    return InternCounterTrack(session_id, source, desc, is_timebase, cpu);
  };

  // Primary (timebase) descriptor: inline, interned, or from defaults.
  std::optional<TrackId> timebase_track_id;
  if (sample.has_primary_descriptor()) {
    CounterDescriptor::Decoder desc(sample.primary_descriptor());
    timebase_track_id = intern_track(desc, /*is_timebase=*/true);
  } else if (sample.has_primary_descriptor_iid()) {
    if (auto* desc = sequence_state->LookupInternedMessage<
                     InternedData::kStackSampleCounterDescriptorsFieldNumber,
                     CounterDescriptor>(sample.primary_descriptor_iid())) {
      timebase_track_id = intern_track(*desc, /*is_timebase=*/true);
    }
  } else if (defaults && defaults->has_primary_descriptor()) {
    CounterDescriptor::Decoder desc(defaults->primary_descriptor());
    timebase_track_id = intern_track(desc, /*is_timebase=*/true);
  }
  if (timebase_track_id && sample.has_primary_weight()) {
    auto counter_id = context_->event_tracker->PushCounter(
        ts, static_cast<double>(sample.primary_weight()), *timebase_track_id);
    if (counter_id) {
      counter_ids.push_back(*counter_id);
    }
  }

  // Follower descriptors: inline, interned, or from defaults. Followers whose
  // counter instance cannot be identified (SCOPE_CPU without a cpu) keep a
  // nullopt slot so the positional weight pairing stays intact.
  std::vector<std::optional<TrackId>> follower_track_ids;
  for (auto it = sample.follower_descriptors(); it; ++it) {
    CounterDescriptor::Decoder desc(*it);
    follower_track_ids.push_back(intern_track(desc, /*is_timebase=*/false));
  }
  bool parse_error = false;
  if (follower_track_ids.empty()) {
    for (auto it = sample.follower_descriptor_iids(&parse_error); it; ++it) {
      auto* desc = sequence_state->LookupInternedMessage<
          InternedData::kStackSampleCounterDescriptorsFieldNumber,
          CounterDescriptor>(*it);
      if (!desc) {
        follower_track_ids.push_back(std::nullopt);
        continue;
      }
      follower_track_ids.push_back(intern_track(*desc, /*is_timebase=*/false));
    }
  }
  if (follower_track_ids.empty() && defaults) {
    for (auto it = defaults->follower_descriptors(); it; ++it) {
      CounterDescriptor::Decoder desc(*it);
      follower_track_ids.push_back(intern_track(desc, /*is_timebase=*/false));
    }
  }

  size_t i = 0;
  for (auto it = sample.follower_weights(&parse_error); it; ++it, ++i) {
    if (i >= follower_track_ids.size()) {
      break;
    }
    if (!follower_track_ids[i]) {
      continue;
    }
    auto counter_id = context_->event_tracker->PushCounter(
        ts, static_cast<double>(*it), *follower_track_ids[i]);
    if (counter_id) {
      counter_ids.push_back(*counter_id);
    }
  }
  return counter_ids;
}

std::optional<tables::ProfilerAsyncContextTable::Id>
StackSampleModule::ResolveAsyncContext(
    PacketSequenceStateGeneration* sequence_state,
    uint64_t iid) {
  using protos::pbzero::InternedData;
  using AsyncContextDescriptor =
      protos::pbzero::StackSample::AsyncContextDescriptor;

  std::vector<InternedMessageView*> unresolved;
  std::optional<tables::ProfilerAsyncContextTable::Id> parent;
  while (iid) {
    InternedMessageView* view = sequence_state->GetInternedMessageView(
        InternedData::kStackSampleAsyncContextDescriptorsFieldNumber, iid);
    if (!view) {
      break;
    }
    if (std::optional<uint32_t> cached = view->cached_value()) {
      parent.emplace(*cached);
      break;
    }
    if (std::find(unresolved.begin(), unresolved.end(), view) !=
        unresolved.end()) {
      break;
    }
    unresolved.push_back(view);
    AsyncContextDescriptor::Decoder* desc =
        view->GetOrCreateDecoder<AsyncContextDescriptor>();
    iid = desc->has_parent_iid() ? desc->parent_iid() : 0;
  }

  for (auto it = unresolved.rbegin(); it != unresolved.rend(); ++it) {
    AsyncContextDescriptor::Decoder* desc =
        (*it)->GetOrCreateDecoder<AsyncContextDescriptor>();
    tables::ProfilerAsyncContextTable::Row row;
    if (desc->name().size > 0) {
      row.name = context_->storage->InternString(desc->name());
    }
    if (desc->kind().size > 0) {
      row.kind = context_->storage->InternString(desc->kind());
    }
    row.parent_id = parent;
    auto id = context_->storage->mutable_profiler_async_context_table()
                  ->Insert(row)
                  .id;
    (*it)->set_cached_value(id.value);
    parent = id;
  }
  return parent;
}

void StackSampleModule::ParseStackSample(
    int64_t ts,
    uint32_t seq_id,
    PacketSequenceStateGeneration* sequence_state,
    protozero::ConstBytes blob) {
  using protos::pbzero::InternedData;
  using protos::pbzero::StackSample;
  using protos::pbzero::StackSampleDefaults;
  using ExecutionContext = StackSample::ExecutionContext;
  using Mode = StackSample::Mode;
  using TaskContext = StackSample::TaskContext;

  StackSample::Decoder sample(blob.data, blob.size);
  TraceStorage* storage = context_->storage.get();

  // Defaults carry the source and the fallback primary descriptor.
  std::optional<StackSampleDefaults::Decoder> defaults;
  if (auto* d = sequence_state->GetTracePacketDefaults();
      d && d->has_stack_sample_defaults()) {
    defaults.emplace(d->stack_sample_defaults());
  }
  StringId source_id = kNullStringId;
  if (defaults && defaults->source().size > 0) {
    source_id = storage->InternString(defaults->source());
  }

  // Task context: attributes the sample to a thread / process / async context.
  std::optional<UniquePid> upid;
  auto intern_task_context = [&](const TaskContext::Decoder& task) {
    tables::ProfilerTaskContextTable::Row row;
    if (task.has_pid()) {
      upid = context_->process_tracker->GetOrCreateProcess(task.pid());
      row.upid = upid;
    }
    if (task.has_tid()) {
      row.utid =
          task.has_pid()
              ? context_->process_tracker->UpdateThread(task.tid(), task.pid())
              : context_->process_tracker->GetOrCreateThread(task.tid());
    }
    if (task.has_async_id()) {
      row.async_context_id =
          ResolveAsyncContext(sequence_state, task.async_id());
    }
    return context_->profiler_sample_tracker->InternTaskContext(row);
  };

  std::optional<tables::ProfilerTaskContextTable::Id> task_context_id;
  if (sample.has_task_context()) {
    TaskContext::Decoder task(sample.task_context());
    task_context_id = intern_task_context(task);
  } else if (sample.has_task_context_iid()) {
    if (auto* task =
            sequence_state->LookupInternedMessage<
                InternedData::kStackSampleTaskContextsFieldNumber, TaskContext>(
                sample.task_context_iid())) {
      task_context_id = intern_task_context(*task);
    }
  }

  // Execution context: cpu + privilege mode at sample time.
  std::optional<uint32_t> cpu;
  auto intern_execution_context = [&](const ExecutionContext::Decoder& exec) {
    tables::ProfilerExecutionContextTable::Row row;
    if (exec.has_cpu()) {
      cpu = exec.cpu();
      row.ucpu = context_->cpu_tracker->GetOrCreateCpu(*cpu).value;
    }
    if (exec.has_mode()) {
      auto mode = static_cast<Mode>(exec.mode());
      if (const char* mode_string = StringifyStackSampleMode(mode)) {
        row.cpu_mode = storage->InternString(mode_string);
      }
    }
    return context_->profiler_sample_tracker->InternExecutionContext(row);
  };

  std::optional<tables::ProfilerExecutionContextTable::Id> execution_context_id;
  if (sample.has_execution_context()) {
    ExecutionContext::Decoder exec(sample.execution_context());
    execution_context_id = intern_execution_context(exec);
  } else if (sample.has_execution_context_iid()) {
    if (auto* exec = sequence_state->LookupInternedMessage<
                     InternedData::kStackSampleExecutionContextsFieldNumber,
                     ExecutionContext>(sample.execution_context_iid())) {
      execution_context_id = intern_execution_context(*exec);
    }
  }

  tables::ProfilerSessionTable::Id session_id =
      GetOrCreateSession(seq_id, source_id);
  std::vector<CounterId> counter_ids = ParseCounterValues(
      ts, sequence_state, session_id, source_id, cpu, sample, defaults);

  tables::ProfilerSampleTable::Row row;
  row.ts = ts;
  row.source = source_id;
  row.session_id = session_id;
  row.task_context_id = task_context_id;
  row.execution_context_id = execution_context_id;
  row.callsite_id = ResolveCallstack(sequence_state, upid, sample);
  row.counter_set_id =
      context_->profiler_sample_tracker->AddCounterSet(counter_ids);
  context_->profiler_sample_tracker->AddSample(row);
}

}  // namespace perfetto::trace_processor::stack_sample_importer
