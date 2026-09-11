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
#include <functional>
#include <memory>
#include <utility>
#include <vector>

#include "perfetto/ext/base/flat_hash_map.h"
#include "perfetto/ext/base/murmur_hash.h"
#include "perfetto/trace_processor/basic_types.h"
#include "src/trace_processor/tables/metadata_tables_py.h"
#include "src/trace_processor/types/destructible.h"
#include "src/trace_processor/types/trace_processor_context_ptr.h"

namespace perfetto::trace_processor {

class ArgsTranslationTable;
class ClockConverter;
class ClockSynchronizer;
class ClockTracker;
class CpuTracker;
class GpuTracker;
class UserTracker;
class DescriptorPool;
class EventTracker;
class FileIoTracker;
class FlowTracker;
class BlobPacketWriter;
class GlobalArgsTracker;
class GlobalMetadataTracker;
class GlobalStatsTracker;
class ImportLogsTracker;
class MachineTracker;
class MappingTracker;
class MetadataTracker;
class StatsTracker;
class ProcessTracker;
class ProcessTrackTranslationTable;
class ProtoTraceReader;
class RegisteredFileTracker;
class SchedEventTracker;
class SliceTracker;
class StateTracker;
class SliceTranslationTable;
class SparseCounterTracker;
class StackProfileTracker;
class ProfilerSampleTracker;
class SymbolTracker;
class TraceDiagnosticsTracker;
class TraceFileTracker;
class TraceImporterRegistry;
class TraceReaderRegistry;
class TraceSorter;
class TraceStorage;
class TraceProcessor_PlatformInterface;

namespace io {
class FileSystem;
}
class TrackCompressor;
class TrackTracker;
struct ProtoImporterModuleContext;
struct TraceManifestState;
struct TraceTimeState;
struct TrackCompressorGroupIdxState;

namespace perf_importer {
class PerfTracker;
}  // namespace perf_importer

using MachineId = tables::MachineTable::Id;
using TraceId = tables::TraceFileTable::Id;

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
    TraceId trace_id;

    // True when a perfetto_manifest override pinned this trace onto a single
    // private clock. The trace must then stay single-clock and single-machine,
    // so ClockSnapshots and remote machine ids are rejected on it.
    bool has_clock_override = false;

    // Set when a perfetto_manifest entry attributed this file to a single
    // machine; lets readers reject it later if it proves to be multi-machine.
    bool has_machine_override = false;

    // For a multi-machine proto file described by a manifest `machines` block:
    // points at that entry's embedded machine_id -> raw machine id map (owned
    // by the manifest state, which outlives parsing). The proto dispatcher
    // forks remote machines onto these instead of rejecting. Null for other
    // files.
    const base::FlatHashMap<uint32_t, int64_t>* machine_remap = nullptr;
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
  static TraceProcessorContext CreateRootContext(
      const Config& config,
      TraceProcessor_PlatformInterface* platform = nullptr) {
    return TraceProcessorContext(config, platform);
  }

  // Destroys all state related to parsing the trace, keeping only state
  // required for querying traces. Must only be called on the root context.
  void DestroyParsingState();

  // Forks the current TraceProcessorContext into a context for parsing a new
  // trace with the given trace id and for adding events for the given machine
  // id.
  TraceProcessorContext* ForkContextForTrace(
      TraceId trace_id,
      int64_t default_raw_machine_id) const;

  // Forks the current TraceProcessorContext into a context for parsing a new
  // machine on the same as the current trace.
  TraceProcessorContext* ForkContextForMachineInCurrentTrace(
      int64_t raw_machine_id) const;

  // Global State
  // ============
  //
  // This state is shared between all machines in a trace.
  // It is initialized once when the root TraceProcessorContext is created and
  // then shared between all machines.

  Config config;
  TraceProcessor_PlatformInterface* platform = nullptr;
  // The filesystem obtained from `platform` at construction. Cached so that
  // each Trace Processor instance uses a stable snapshot of its platform's
  // filesystem for its whole lifetime. Borrowed; must outlive this instance.
  io::FileSystem* file_system = nullptr;
  GlobalPtr<TraceStorage> storage;
  GlobalPtr<TraceSorter> sorter;
  GlobalPtr<TraceReaderRegistry> reader_registry;
  // Non-owning view of the trace-type metadata + plugin importers owned by
  // `reader_registry`. Exposed here so low-layer code (trace_file_tracker,
  // archive readers) can query per-type metadata without depending on the
  // reader registry header.
  TraceImporterRegistry* trace_importer_registry = nullptr;
  GlobalPtr<GlobalArgsTracker> global_args_tracker;
  GlobalPtr<GlobalMetadataTracker> global_metadata_tracker;
  GlobalPtr<GlobalStatsTracker> global_stats_tracker;
  GlobalPtr<TraceFileTracker> trace_file_tracker;
  GlobalPtr<DescriptorPool> descriptor_pool_;
  GlobalPtr<ForkedContextState> forked_context_state;
  GlobalPtr<ClockConverter> clock_converter;
  GlobalPtr<TraceTimeState> trace_time_state;
  // One clock graph for the whole import. Clocks are isolated per-(machine,
  // file) via ClockId, so cross-machine/cross-file syncs are ordinary edges.
  GlobalPtr<ClockSynchronizer> clock_sync;
  GlobalPtr<TraceManifestState> trace_manifest_state;
  GlobalPtr<TrackCompressorGroupIdxState> track_group_idx_state;
  GlobalPtr<StackProfileTracker> stack_profile_tracker;
  GlobalPtr<ProfilerSampleTracker> profiler_sample_tracker;
  GlobalPtr<Destructible> deobfuscation_tracker;  // DeobfuscationTracker
  GlobalPtr<BlobPacketWriter> blob_packet_writer;

  // The registration function for additional proto modules.
  // This is populated by TraceProcessorImpl to allow for late registration of
  // modules.
  using RegisterAdditionalProtoModulesFn =
      std::function<void(ProtoImporterModuleContext*, TraceProcessorContext*)>;
  RegisterAdditionalProtoModulesFn register_additional_proto_modules;

  // Registry of callbacks invoked when PerfTracker is created, allowing
  // external code (e.g. ETM) to register aux tokenizers.
  using PerfAuxTokenizerRegistration =
      std::function<void(perf_importer::PerfTracker*)>;
  std::vector<PerfAuxTokenizerRegistration> perf_aux_tokenizer_registrations;

  // Per-Trace State (Miscategorized)
  // ==========================
  //
  // This state is shared between all machines in a trace but is specific to a
  // single trace.
  //
  // TODO(lalitm): this is miscategorized due to legacy reasons. It needs to be
  // moved to a "per-trace" category.

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
  PerTracePtr<TraceDiagnosticsTracker> trace_diagnostics_tracker;

  // Per-Machine State
  // =================
  //
  // This state is unique to each machine in a trace.
  // It is initialized when a new machine is discovered.

  PerMachinePtr<SymbolTracker> symbol_tracker;
  PerMachinePtr<ProcessTracker> process_tracker;
  PerMachinePtr<MappingTracker> mapping_tracker;
  PerMachinePtr<MachineTracker> machine_tracker;
  PerMachinePtr<CpuTracker> cpu_tracker;
  PerMachinePtr<GpuTracker> gpu_tracker;
  PerMachinePtr<UserTracker> user_tracker;

  // Per-Machine, Per-Trace State
  // ==========================
  //
  // This state is unique to each (machine, trace) pair.

  PerTraceAndMachinePtr<ClockTracker> clock_tracker;
  PerTraceAndMachinePtr<ArgsTranslationTable> args_translation_table;
  PerTraceAndMachinePtr<ProcessTrackTranslationTable>
      process_track_translation_table;
  PerTraceAndMachinePtr<SliceTranslationTable> slice_translation_table;
  PerTraceAndMachinePtr<TrackTracker> track_tracker;
  PerTraceAndMachinePtr<TrackCompressor> track_compressor;
  PerTraceAndMachinePtr<SliceTracker> slice_tracker;
  PerTraceAndMachinePtr<StateTracker> state_tracker;
  PerTraceAndMachinePtr<FileIoTracker> file_io_tracker;
  PerTraceAndMachinePtr<FlowTracker> flow_tracker;
  PerTraceAndMachinePtr<EventTracker> event_tracker;
  PerTraceAndMachinePtr<SchedEventTracker> sched_event_tracker;
  PerTraceAndMachinePtr<MetadataTracker> metadata_tracker;
  PerTraceAndMachinePtr<StatsTracker> stats_tracker;
  PerTraceAndMachinePtr<SparseCounterTracker> sparse_counter_tracker;

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

  MachineId machine_id() const;
  TraceId trace_id() const;

  // True when a perfetto_manifest clock override constrains this trace to a
  // single clock and machine. False for root/container contexts that have no
  // per-trace state.
  bool has_clock_override() const {
    return trace_state && trace_state->has_clock_override;
  }

  // True when a perfetto_manifest entry attributed this trace to a single
  // machine. False for root/container contexts that have no per-trace state.
  bool has_machine_override() const {
    return trace_state && trace_state->has_machine_override;
  }

 private:
  TraceProcessorContext(const Config& config,
                        TraceProcessor_PlatformInterface* platform);

  TraceProcessorContext(TraceProcessorContext&&) = default;
  TraceProcessorContext& operator=(TraceProcessorContext&&) = default;
};

class TraceProcessorContext::ForkedContextState {
 public:
  using TraceIdAndMachineId = std::pair<uint32_t, int64_t>;
  base::FlatHashMap<TraceIdAndMachineId,
                    std::unique_ptr<TraceProcessorContext>,
                    base::MurmurHash<TraceIdAndMachineId>>
      trace_and_machine_to_context;
  base::FlatHashMap<uint32_t, TraceProcessorContext*> trace_to_context;
  base::FlatHashMap<int64_t, TraceProcessorContext*> machine_to_context;
};

}  // namespace perfetto::trace_processor

#endif  // SRC_TRACE_PROCESSOR_TYPES_TRACE_PROCESSOR_CONTEXT_H_
