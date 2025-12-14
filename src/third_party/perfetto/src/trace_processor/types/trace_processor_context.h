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

#ifndef SRC_TRACE_PROCESSOR_TYPES_TRACE_PROCESSOR_CONTEXT_H_
#define SRC_TRACE_PROCESSOR_TYPES_TRACE_PROCESSOR_CONTEXT_H_

#include <cstdint>
#include <memory>
#include <optional>
#include <utility>

#include "perfetto/ext/base/flat_hash_map.h"
#include "perfetto/ext/base/murmur_hash.h"
#include "perfetto/trace_processor/basic_types.h"
#include "src/trace_processor/tables/metadata_tables_py.h"
#include "src/trace_processor/types/destructible.h"
#include "src/trace_processor/types/trace_processor_context_ptr.h"

namespace perfetto::trace_processor {

class ClockSynchronizerListenerImpl;
class ArgsTranslationTable;
class ClockConverter;
template <typename T>
class ClockSynchronizer;
class CpuTracker;
class DescriptorPool;
class EventTracker;
class FlowTracker;
class GlobalArgsTracker;
class ImportLogsTracker;
class MachineTracker;
class MappingTracker;
class MetadataTracker;
class ProcessTracker;
class ProcessTrackTranslationTable;
class ProtoTraceReader;
class RegisteredFileTracker;
class SchedEventTracker;
class SliceTracker;
class SliceTranslationTable;
class StackProfileTracker;
class SymbolTracker;
class TraceFileTracker;
class TraceReaderRegistry;
class TraceSorter;
class TraceStorage;
class TrackCompressor;
class TrackTracker;
struct ProtoImporterModuleContext;
struct TrackCompressorGroupIdxState;

using MachineId = tables::MachineTable::Id;
using ClockTracker = ClockSynchronizer<ClockSynchronizerListenerImpl>;

class TraceProcessorContext {
 public:
  template <typename T>
  using GlobalPtr = TraceProcessorContextPtr<T>;

  template <typename T>
  using RootPtr = TraceProcessorContextPtr<T>;

  template <typename T>
  using PerMachinePtr = TraceProcessorContextPtr<T>;

  template <typename T>
  using PerTracePtr = TraceProcessorContextPtr<T>;

  template <typename T>
  using PerTraceAndMachinePtr = TraceProcessorContextPtr<T>;

  class ForkedContextState;

  struct TraceState {
    uint32_t raw_trace_id = 0;
  };

  struct UuidState {
    // Marks whether the uuid was read from the trace.
    // If the uuid was NOT read, the uuid will be made from the hash of the
    // first 4KB of the trace.
    bool uuid_found_in_trace = false;
  };

  // The default constructor is used in testing.
  TraceProcessorContext();
  ~TraceProcessorContext();

  TraceProcessorContext(const TraceProcessorContext&) = delete;
  TraceProcessorContext& operator=(const TraceProcessorContext&) = delete;

  // Creates the root TraceProcessorContext. Should only be called by
  // TraceProcessor top level class.
  static TraceProcessorContext CreateRootContext(const Config& config) {
    return TraceProcessorContext(config);
  }

  // Destroys all state related to parsing the trace, keeping only state
  // required for querying traces. Must only be called on the root context.
  void DestroyParsingState();

  // Forks the current TraceProcessorContext into a context for parsing a new
  // trace with the given trace id and for adding events for the given machine
  // id.
  TraceProcessorContext* ForkContextForTrace(
      uint32_t raw_trace_id,
      uint32_t default_raw_machine_id) const;

  // Forks the current TraceProcessorContext into a context for parsing a new
  // machine on the same as the current trace.
  TraceProcessorContext* ForkContextForMachineInCurrentTrace(
      uint32_t raw_machine_id) const;

  // Global State
  // ============
  //
  // This state is shared between all machines in a trace.
  // It is initialized once when the root TraceProcessorContext is created and
  // then shared between all machines.

  Config config;
  GlobalPtr<TraceStorage> storage;
  GlobalPtr<TraceSorter> sorter;
  GlobalPtr<TraceReaderRegistry> reader_registry;
  GlobalPtr<GlobalArgsTracker> global_args_tracker;
  GlobalPtr<TraceFileTracker> trace_file_tracker;
  GlobalPtr<DescriptorPool> descriptor_pool_;
  GlobalPtr<ForkedContextState> forked_context_state;
  GlobalPtr<ClockConverter> clock_converter;
  GlobalPtr<TrackCompressorGroupIdxState> track_group_idx_state;
  GlobalPtr<StackProfileTracker> stack_profile_tracker;
  GlobalPtr<Destructible> deobfuscation_tracker;  // DeobfuscationTracker

  // The registration function for additional proto modules.
  // This is populated by TraceProcessorImpl to allow for late registration of
  // modules.
  using RegisterAdditionalProtoModulesFn = void(ProtoImporterModuleContext*,
                                                TraceProcessorContext*);
  RegisterAdditionalProtoModulesFn* register_additional_proto_modules = nullptr;

  // Per-Trace State (Miscategorized)
  // ==========================
  //
  // This state is shared between all machines in a trace but is specific to a
  // single trace.
  //
  // TODO(lalitm): this is miscategorized due to legacy reasons. It needs to be
  // moved to a "per-trace" category.

  GlobalPtr<MetadataTracker> metadata_tracker;
  GlobalPtr<RegisteredFileTracker> registered_file_tracker;
  GlobalPtr<UuidState> uuid_state;
  GlobalPtr<Destructible> heap_graph_tracker;  // HeapGraphTracker

  // Per-Trace State
  // ==========================
  //
  // This state is shared between all machines in a trace but is specific to a
  // single trace.
  // It is initialized when a new trace is discovered.

  PerTracePtr<TraceState> trace_state;
  PerTracePtr<Destructible> content_analyzer;
  PerTracePtr<ImportLogsTracker> import_logs_tracker;

  // Per-Machine State
  // =================
  //
  // This state is unique to each machine in a trace.
  // It is initialized when a new machine is discovered.

  PerMachinePtr<SymbolTracker> symbol_tracker;
  PerMachinePtr<ProcessTracker> process_tracker;
  PerMachinePtr<ClockTracker> clock_tracker;
  PerMachinePtr<MappingTracker> mapping_tracker;
  PerMachinePtr<MachineTracker> machine_tracker;
  PerMachinePtr<CpuTracker> cpu_tracker;

  // Per-Machine, Per-Trace State
  // ==========================
  //
  // This state is unique to each (machine, trace) pair.

  PerTraceAndMachinePtr<ArgsTranslationTable> args_translation_table;
  PerTraceAndMachinePtr<ProcessTrackTranslationTable>
      process_track_translation_table;
  PerTraceAndMachinePtr<SliceTranslationTable> slice_translation_table;
  PerTraceAndMachinePtr<TrackTracker> track_tracker;
  PerTraceAndMachinePtr<TrackCompressor> track_compressor;
  PerTraceAndMachinePtr<SliceTracker> slice_tracker;
  PerTraceAndMachinePtr<FlowTracker> flow_tracker;
  PerTraceAndMachinePtr<EventTracker> event_tracker;
  PerTraceAndMachinePtr<SchedEventTracker> sched_event_tracker;

  // These fields are stored as pointers to Destructible objects rather than
  // their actual type (a subclass of Destructible), as the concrete subclass
  // type is only available in storage_full target. To access these fields use
  // the GetOrCreate() method on their subclass type, e.g.
  // SyscallTracker::GetOrCreate(context)
  PerTraceAndMachinePtr<Destructible> binder_tracker;       // BinderTracker
  PerTraceAndMachinePtr<Destructible> syscall_tracker;      // SyscallTracker
  PerTraceAndMachinePtr<Destructible> system_info_tracker;  // SystemInfoTracker
  PerTraceAndMachinePtr<Destructible> systrace_parser;      // SystraceParser
  PerTraceAndMachinePtr<Destructible>
      thread_state_tracker;  // ThreadStateTracker
  PerTraceAndMachinePtr<Destructible>
      ftrace_sched_tracker;  // FtraceSchedEventTracker

  std::optional<MachineId> machine_id() const;

 private:
  explicit TraceProcessorContext(const Config& config);

  TraceProcessorContext(TraceProcessorContext&&) = default;
  TraceProcessorContext& operator=(TraceProcessorContext&&) = default;
};

class TraceProcessorContext::ForkedContextState {
 public:
  using TraceIdAndMachineId = std::pair<uint32_t, uint32_t>;
  struct Hasher {
    uint64_t operator()(const TraceIdAndMachineId& key) {
      return base::MurmurHashCombine(key.first, key.second);
    }
  };
  base::FlatHashMap<TraceIdAndMachineId,
                    std::unique_ptr<TraceProcessorContext>,
                    Hasher>
      trace_and_machine_to_context;
  base::FlatHashMap<uint32_t, TraceProcessorContext*> trace_to_context;
  base::FlatHashMap<uint32_t, TraceProcessorContext*> machine_to_context;
};

}  // namespace perfetto::trace_processor

#endif  // SRC_TRACE_PROCESSOR_TYPES_TRACE_PROCESSOR_CONTEXT_H_
