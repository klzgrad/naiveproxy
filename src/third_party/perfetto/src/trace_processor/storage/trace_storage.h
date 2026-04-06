/*
 * Copyright (C) 2017 The Android Open Source Project
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

#ifndef SRC_TRACE_PROCESSOR_STORAGE_TRACE_STORAGE_H_
#define SRC_TRACE_PROCESSOR_STORAGE_TRACE_STORAGE_H_

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <functional>
#include <iterator>
#include <limits>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "perfetto/base/logging.h"
#include "perfetto/base/time.h"
#include "perfetto/ext/base/string_view.h"
#include "perfetto/trace_processor/basic_types.h"
#include "perfetto/trace_processor/trace_blob_view.h"
#include "src/trace_processor/containers/null_term_string_view.h"
#include "src/trace_processor/containers/string_pool.h"
#include "src/trace_processor/core/dataframe/dataframe.h"
#include "src/trace_processor/storage/stats.h"
#include "src/trace_processor/tables/all_tables_fwd.h"
#include "src/trace_processor/types/destructible.h"
#include "src/trace_processor/types/variadic.h"

namespace perfetto::trace_processor {
namespace etm {
class TargetMemory;
}

// UniquePid is an offset into |unique_processes_|. This is necessary because
// Unix pids are reused and thus not guaranteed to be unique over a long
// period of time.
using UniquePid = uint32_t;

// UniqueTid is an offset into |unique_threads_|. Necessary because tids can
// be reused.
using UniqueTid = uint32_t;

// StringId is an offset into |string_pool_|.
using StringId = StringPool::Id;
static const StringId kNullStringId = StringId::Null();

using ArgSetId = uint32_t;

using TrackId = tables::TrackTable_Id;

using CounterId = tables::CounterTable_Id;

using SliceId = tables::SliceTable_Id;

using SchedId = tables::SchedSliceTable_Id;

using MappingId = tables::StackProfileMappingTable_Id;

using FrameId = tables::StackProfileFrameTable_Id;

using SymbolId = tables::SymbolTable_Id;

using CallsiteId = tables::StackProfileCallsiteTable_Id;

using MetadataId = tables::MetadataTable_Id;

using VulkanAllocId = tables::VulkanMemoryAllocationsTable_Id;

using ProcessMemorySnapshotId = tables::ProcessMemorySnapshotTable_Id;

using SnapshotNodeId = tables::MemorySnapshotNodeTable_Id;

using TrackEventCallstacksId = tables::TrackEventCallstacksTable_Id;

static const TrackId kInvalidTrackId =
    TrackId(std::numeric_limits<uint32_t>::max());

static constexpr uint32_t kDefaultMachineId = 0;

// Stores a data inside a trace file in a columnar form. This makes it efficient
// to read or search across a single field of the trace (e.g. all the thread
// names for a given CPU).
class TraceStorage {
 public:
  explicit TraceStorage(const Config& = Config());

  virtual ~TraceStorage();

  class VirtualTrackSlices {
   public:
    uint32_t AddVirtualTrackSlice(SliceId slice_id,
                                  int64_t thread_timestamp_ns,
                                  int64_t thread_duration_ns,
                                  int64_t thread_instruction_count,
                                  int64_t thread_instruction_delta) {
      slice_ids_.emplace_back(slice_id);
      thread_timestamp_ns_.emplace_back(thread_timestamp_ns);
      thread_duration_ns_.emplace_back(thread_duration_ns);
      thread_instruction_counts_.emplace_back(thread_instruction_count);
      thread_instruction_deltas_.emplace_back(thread_instruction_delta);
      return slice_count() - 1;
    }

    uint32_t slice_count() const {
      return static_cast<uint32_t>(slice_ids_.size());
    }

    const std::deque<SliceId>& slice_ids() const { return slice_ids_; }
    const std::deque<int64_t>& thread_timestamp_ns() const {
      return thread_timestamp_ns_;
    }
    const std::deque<int64_t>& thread_duration_ns() const {
      return thread_duration_ns_;
    }
    const std::deque<int64_t>& thread_instruction_counts() const {
      return thread_instruction_counts_;
    }
    const std::deque<int64_t>& thread_instruction_deltas() const {
      return thread_instruction_deltas_;
    }

    std::optional<uint32_t> FindRowForSliceId(SliceId slice_id) const {
      auto it =
          std::lower_bound(slice_ids().begin(), slice_ids().end(), slice_id);
      if (it != slice_ids().end() && *it == slice_id) {
        return static_cast<uint32_t>(std::distance(slice_ids().begin(), it));
      }
      return std::nullopt;
    }

    void UpdateThreadDeltasForSliceId(SliceId slice_id,
                                      int64_t end_thread_timestamp_ns,
                                      int64_t end_thread_instruction_count) {
      auto opt_row = FindRowForSliceId(slice_id);
      if (!opt_row)
        return;
      uint32_t row = *opt_row;
      int64_t begin_ns = thread_timestamp_ns_[row];
      thread_duration_ns_[row] = end_thread_timestamp_ns - begin_ns;
      int64_t begin_ticount = thread_instruction_counts_[row];
      thread_instruction_deltas_[row] =
          end_thread_instruction_count - begin_ticount;
    }

   private:
    std::deque<SliceId> slice_ids_;
    std::deque<int64_t> thread_timestamp_ns_;
    std::deque<int64_t> thread_duration_ns_;
    std::deque<int64_t> thread_instruction_counts_;
    std::deque<int64_t> thread_instruction_deltas_;
  };

  class SqlStats {
   public:
    static constexpr size_t kMaxLogEntries = 100;
    uint32_t RecordQueryBegin(const std::string& query, int64_t time_started);
    void RecordQueryFirstNext(uint32_t row, int64_t time_first_next);
    void RecordQueryEnd(uint32_t row, int64_t time_end);
    size_t size() const { return queries_.size(); }
    const std::deque<std::string>& queries() const { return queries_; }
    const std::deque<int64_t>& times_started() const { return times_started_; }
    const std::deque<int64_t>& times_first_next() const {
      return times_first_next_;
    }
    const std::deque<int64_t>& times_ended() const { return times_ended_; }

   private:
    uint32_t popped_queries_ = 0;

    std::deque<std::string> queries_;
    std::deque<int64_t> times_started_;
    std::deque<int64_t> times_first_next_;
    std::deque<int64_t> times_ended_;
  };

  struct Stats {
    using IndexMap = std::map<int, int64_t>;
    int64_t value = 0;
    IndexMap indexed_values;
  };
  using StatsMap = std::array<Stats, stats::kNumKeys>;

  // Return an unique identifier for the contents of each string.
  // The string is copied internally and can be destroyed after this called.
  // Virtual for testing.
  virtual StringId InternString(base::StringView str) {
    return string_pool_.InternString(str);
  }
  virtual StringId InternString(const char* str) {
    return InternString(base::StringView(str));
  }
  virtual StringId InternString(const std::string& str) {
    return InternString(base::StringView(str));
  }
  virtual StringId InternString(std::string_view str) {
    return InternString(base::StringView(str.data(), str.size()));
  }

  // Example usage: SetStats(stats::android_log_num_failed, 42);
  // TODO(lalitm): make these correctly work across machines and across
  // traces.
  void SetStats(size_t key, int64_t value) {
    PERFETTO_DCHECK(key < stats::kNumKeys);
    PERFETTO_DCHECK(stats::kTypes[key] == stats::kSingle);
    stats_[key].value = value;
  }

  // Example usage: IncrementStats(stats::android_log_num_failed, -1);
  // TODO(lalitm): make these correctly work across machines and across
  // traces.
  void IncrementStats(size_t key, int64_t increment = 1) {
    PERFETTO_DCHECK(key < stats::kNumKeys);
    PERFETTO_DCHECK(stats::kTypes[key] == stats::kSingle);
    stats_[key].value += increment;
  }

  // Example usage: IncrementIndexedStats(stats::cpu_failure, 1);
  // TODO(lalitm): make these correctly work across machines and across
  // traces.
  void IncrementIndexedStats(size_t key, int index, int64_t increment = 1) {
    PERFETTO_DCHECK(key < stats::kNumKeys);
    PERFETTO_DCHECK(stats::kTypes[key] == stats::kIndexed);
    stats_[key].indexed_values[index] += increment;
  }

  // Example usage: SetIndexedStats(stats::cpu_failure, 1, 42);
  // TODO(lalitm): make these correctly work across machines and across
  // traces.
  void SetIndexedStats(size_t key, int index, int64_t value) {
    PERFETTO_DCHECK(key < stats::kNumKeys);
    PERFETTO_DCHECK(stats::kTypes[key] == stats::kIndexed);
    stats_[key].indexed_values[index] = value;
  }

  // Example usage: opt_cpu_failure = GetIndexedStats(stats::cpu_failure, 1);
  // TODO(lalitm): make these correctly work across machines and across
  // traces.
  std::optional<int64_t> GetIndexedStats(size_t key, int index) {
    PERFETTO_DCHECK(key < stats::kNumKeys);
    PERFETTO_DCHECK(stats::kTypes[key] == stats::kIndexed);
    auto kv = stats_[key].indexed_values.find(index);
    if (kv != stats_[key].indexed_values.end()) {
      return kv->second;
    }
    return std::nullopt;
  }

  // TODO(lalitm): make these correctly work across machines and across
  // traces.
  int64_t GetStats(size_t key) {
    PERFETTO_DCHECK(key < stats::kNumKeys);
    PERFETTO_DCHECK(stats::kTypes[key] == stats::kSingle);
    return stats_[key].value;
  }

  class ScopedStatsTracer {
   public:
    ScopedStatsTracer(TraceStorage* storage, size_t key)
        : storage_(storage), key_(key), start_ns_(base::GetWallTimeNs()) {}

    ~ScopedStatsTracer() {
      if (!storage_)
        return;
      auto delta_ns = base::GetWallTimeNs() - start_ns_;
      storage_->IncrementStats(key_, delta_ns.count());
    }

    ScopedStatsTracer(ScopedStatsTracer&& other) noexcept { MoveImpl(&other); }

    ScopedStatsTracer& operator=(ScopedStatsTracer&& other) {
      MoveImpl(&other);
      return *this;
    }

   private:
    ScopedStatsTracer(const ScopedStatsTracer&) = delete;
    ScopedStatsTracer& operator=(const ScopedStatsTracer&) = delete;

    void MoveImpl(ScopedStatsTracer* other) {
      storage_ = other->storage_;
      key_ = other->key_;
      start_ns_ = other->start_ns_;
      other->storage_ = nullptr;
    }

    TraceStorage* storage_;
    size_t key_;
    base::TimeNanos start_ns_;
  };

  ScopedStatsTracer TraceExecutionTimeIntoStats(size_t key) {
    return ScopedStatsTracer(this, key);
  }

  // Reading methods.
  // Virtual for testing.
  virtual NullTermStringView GetString(std::optional<StringId> id) const {
    return id ? string_pool_.Get(*id) : NullTermStringView();
  }

  const tables::ThreadTable& thread_table() const {
    return table<tables::ThreadTable>();
  }
  tables::ThreadTable* mutable_thread_table() {
    return mutable_table<tables::ThreadTable>();
  }

  const tables::ProcessTable& process_table() const {
    return table<tables::ProcessTable>();
  }
  tables::ProcessTable* mutable_process_table() {
    return mutable_table<tables::ProcessTable>();
  }

  const tables::FiledescriptorTable& filedescriptor_table() const {
    return table<tables::FiledescriptorTable>();
  }
  tables::FiledescriptorTable* mutable_filedescriptor_table() {
    return mutable_table<tables::FiledescriptorTable>();
  }

  const tables::TrackTable& track_table() const {
    return table<tables::TrackTable>();
  }
  tables::TrackTable* mutable_track_table() {
    return mutable_table<tables::TrackTable>();
  }

  const tables::GpuCounterGroupTable& gpu_counter_group_table() const {
    return table<tables::GpuCounterGroupTable>();
  }
  tables::GpuCounterGroupTable* mutable_gpu_counter_group_table() {
    return mutable_table<tables::GpuCounterGroupTable>();
  }

  const tables::ThreadStateTable& thread_state_table() const {
    return table<tables::ThreadStateTable>();
  }
  tables::ThreadStateTable* mutable_thread_state_table() {
    return mutable_table<tables::ThreadStateTable>();
  }

  const tables::SchedSliceTable& sched_slice_table() const {
    return table<tables::SchedSliceTable>();
  }
  tables::SchedSliceTable* mutable_sched_slice_table() {
    return mutable_table<tables::SchedSliceTable>();
  }

  const tables::SliceTable& slice_table() const {
    return table<tables::SliceTable>();
  }
  tables::SliceTable* mutable_slice_table() {
    return mutable_table<tables::SliceTable>();
  }

  const tables::TrackEventCallstacksTable& track_event_callstacks_table()
      const {
    return table<tables::TrackEventCallstacksTable>();
  }
  tables::TrackEventCallstacksTable* mutable_track_event_callstacks_table() {
    return mutable_table<tables::TrackEventCallstacksTable>();
  }

  const tables::SpuriousSchedWakeupTable& spurious_sched_wakeup_table() const {
    return table<tables::SpuriousSchedWakeupTable>();
  }
  tables::SpuriousSchedWakeupTable* mutable_spurious_sched_wakeup_table() {
    return mutable_table<tables::SpuriousSchedWakeupTable>();
  }

  const tables::FlowTable& flow_table() const {
    return table<tables::FlowTable>();
  }
  tables::FlowTable* mutable_flow_table() {
    return mutable_table<tables::FlowTable>();
  }

  const VirtualTrackSlices& virtual_track_slices() const {
    return virtual_track_slices_;
  }
  VirtualTrackSlices* mutable_virtual_track_slices() {
    return &virtual_track_slices_;
  }

  const tables::CounterTable& counter_table() const {
    return table<tables::CounterTable>();
  }
  tables::CounterTable* mutable_counter_table() {
    return mutable_table<tables::CounterTable>();
  }

  const SqlStats& sql_stats() const { return sql_stats_; }
  SqlStats* mutable_sql_stats() { return &sql_stats_; }

  const tables::AndroidCpuPerUidTrackTable& android_cpu_per_uid_track_table()
      const {
    return table<tables::AndroidCpuPerUidTrackTable>();
  }
  tables::AndroidCpuPerUidTrackTable*
  mutable_android_cpu_per_uid_track_table() {
    return mutable_table<tables::AndroidCpuPerUidTrackTable>();
  }

  const tables::AndroidLogTable& android_log_table() const {
    return table<tables::AndroidLogTable>();
  }
  tables::AndroidLogTable* mutable_android_log_table() {
    return mutable_table<tables::AndroidLogTable>();
  }

  const tables::AndroidDumpstateTable& android_dumpstate_table() const {
    return table<tables::AndroidDumpstateTable>();
  }

  tables::AndroidDumpstateTable* mutable_android_dumpstate_table() {
    return mutable_table<tables::AndroidDumpstateTable>();
  }

  const tables::AndroidKeyEventsTable& android_key_events_table() const {
    return table<tables::AndroidKeyEventsTable>();
  }
  tables::AndroidKeyEventsTable* mutable_android_key_events_table() {
    return mutable_table<tables::AndroidKeyEventsTable>();
  }

  const tables::AndroidMotionEventsTable& android_motion_events_table() const {
    return table<tables::AndroidMotionEventsTable>();
  }
  tables::AndroidMotionEventsTable* mutable_android_motion_events_table() {
    return mutable_table<tables::AndroidMotionEventsTable>();
  }

  const tables::AndroidInputEventDispatchTable&
  android_input_event_dispatch_table() const {
    return table<tables::AndroidInputEventDispatchTable>();
  }
  tables::AndroidInputEventDispatchTable*
  mutable_android_input_event_dispatch_table() {
    return mutable_table<tables::AndroidInputEventDispatchTable>();
  }

  const StatsMap& stats() const { return stats_; }

  const tables::MetadataTable& metadata_table() const {
    return table<tables::MetadataTable>();
  }
  tables::MetadataTable* mutable_metadata_table() {
    return mutable_table<tables::MetadataTable>();
  }

  const tables::BuildFlagsTable& build_flags_table() const {
    return table<tables::BuildFlagsTable>();
  }

  tables::BuildFlagsTable* mutable_build_flags_table() {
    return mutable_table<tables::BuildFlagsTable>();
  }

  const tables::ModulesTable& modules_table() const {
    return table<tables::ModulesTable>();
  }

  tables::ModulesTable* mutable_modules_table() {
    return mutable_table<tables::ModulesTable>();
  }

  const tables::TraceImportLogsTable& trace_import_logs_table() const {
    return table<tables::TraceImportLogsTable>();
  }

  tables::TraceImportLogsTable* mutable_trace_import_logs_table() {
    return mutable_table<tables::TraceImportLogsTable>();
  }

  const tables::ClockSnapshotTable& clock_snapshot_table() const {
    return table<tables::ClockSnapshotTable>();
  }
  tables::ClockSnapshotTable* mutable_clock_snapshot_table() {
    return mutable_table<tables::ClockSnapshotTable>();
  }

  const tables::ArgTable& arg_table() const {
    return table<tables::ArgTable>();
  }
  tables::ArgTable* mutable_arg_table() {
    return mutable_table<tables::ArgTable>();
  }

  const tables::ChromeRawTable& chrome_raw_table() const {
    return table<tables::ChromeRawTable>();
  }
  tables::ChromeRawTable* mutable_chrome_raw_table() {
    return mutable_table<tables::ChromeRawTable>();
  }

  const tables::FtraceEventTable& ftrace_event_table() const {
    return table<tables::FtraceEventTable>();
  }
  tables::FtraceEventTable* mutable_ftrace_event_table() {
    return mutable_table<tables::FtraceEventTable>();
  }

  const tables::MachineTable& machine_table() const {
    return table<tables::MachineTable>();
  }
  tables::MachineTable* mutable_machine_table() {
    return mutable_table<tables::MachineTable>();
  }

  const tables::CpuTable& cpu_table() const {
    return table<tables::CpuTable>();
  }
  tables::CpuTable* mutable_cpu_table() {
    return mutable_table<tables::CpuTable>();
  }

  const tables::CpuFreqTable& cpu_freq_table() const {
    return table<tables::CpuFreqTable>();
  }
  tables::CpuFreqTable* mutable_cpu_freq_table() {
    return mutable_table<tables::CpuFreqTable>();
  }

  const tables::StackProfileMappingTable& stack_profile_mapping_table() const {
    return table<tables::StackProfileMappingTable>();
  }
  tables::StackProfileMappingTable* mutable_stack_profile_mapping_table() {
    return mutable_table<tables::StackProfileMappingTable>();
  }

  const tables::StackProfileFrameTable& stack_profile_frame_table() const {
    return table<tables::StackProfileFrameTable>();
  }
  tables::StackProfileFrameTable* mutable_stack_profile_frame_table() {
    return mutable_table<tables::StackProfileFrameTable>();
  }

  const tables::StackProfileCallsiteTable& stack_profile_callsite_table()
      const {
    return table<tables::StackProfileCallsiteTable>();
  }
  tables::StackProfileCallsiteTable* mutable_stack_profile_callsite_table() {
    return mutable_table<tables::StackProfileCallsiteTable>();
  }

  const tables::HeapProfileAllocationTable& heap_profile_allocation_table()
      const {
    return table<tables::HeapProfileAllocationTable>();
  }
  tables::HeapProfileAllocationTable* mutable_heap_profile_allocation_table() {
    return mutable_table<tables::HeapProfileAllocationTable>();
  }

  const tables::PackageListTable& package_list_table() const {
    return table<tables::PackageListTable>();
  }
  tables::PackageListTable* mutable_package_list_table() {
    return mutable_table<tables::PackageListTable>();
  }

  const tables::AndroidUserListTable& user_list_table() const {
    return table<tables::AndroidUserListTable>();
  }
  tables::AndroidUserListTable* mutable_user_list_table() {
    return mutable_table<tables::AndroidUserListTable>();
  }

  const tables::AndroidGameInterventionListTable&
  android_game_intervention_list_table() const {
    return table<tables::AndroidGameInterventionListTable>();
  }
  tables::AndroidGameInterventionListTable*
  mutable_android_game_intervenion_list_table() {
    return mutable_table<tables::AndroidGameInterventionListTable>();
  }

  const tables::ProfilerSmapsTable& profiler_smaps_table() const {
    return table<tables::ProfilerSmapsTable>();
  }
  tables::ProfilerSmapsTable* mutable_profiler_smaps_table() {
    return mutable_table<tables::ProfilerSmapsTable>();
  }

  const tables::TraceFileTable& trace_file_table() const {
    return table<tables::TraceFileTable>();
  }
  tables::TraceFileTable* mutable_trace_file_table() {
    return mutable_table<tables::TraceFileTable>();
  }

  const tables::CpuProfileStackSampleTable& cpu_profile_stack_sample_table()
      const {
    return table<tables::CpuProfileStackSampleTable>();
  }
  tables::CpuProfileStackSampleTable* mutable_cpu_profile_stack_sample_table() {
    return mutable_table<tables::CpuProfileStackSampleTable>();
  }

  const tables::PerfSessionTable& perf_session_table() const {
    return table<tables::PerfSessionTable>();
  }
  tables::PerfSessionTable* mutable_perf_session_table() {
    return mutable_table<tables::PerfSessionTable>();
  }

  const tables::PerfSampleTable& perf_sample_table() const {
    return table<tables::PerfSampleTable>();
  }
  tables::PerfSampleTable* mutable_perf_sample_table() {
    return mutable_table<tables::PerfSampleTable>();
  }

  const tables::PerfCounterSetTable& perf_counter_set_table() const {
    return table<tables::PerfCounterSetTable>();
  }
  tables::PerfCounterSetTable* mutable_perf_counter_set_table() {
    return mutable_table<tables::PerfCounterSetTable>();
  }

  const tables::InstrumentsSampleTable& instruments_sample_table() const {
    return table<tables::InstrumentsSampleTable>();
  }
  tables::InstrumentsSampleTable* mutable_instruments_sample_table() {
    return mutable_table<tables::InstrumentsSampleTable>();
  }

  const tables::SymbolTable& symbol_table() const {
    return table<tables::SymbolTable>();
  }

  tables::SymbolTable* mutable_symbol_table() {
    return mutable_table<tables::SymbolTable>();
  }

  const tables::HeapGraphObjectTable& heap_graph_object_table() const {
    return table<tables::HeapGraphObjectTable>();
  }

  tables::HeapGraphObjectTable* mutable_heap_graph_object_table() {
    return mutable_table<tables::HeapGraphObjectTable>();
  }
  const tables::HeapGraphClassTable& heap_graph_class_table() const {
    return table<tables::HeapGraphClassTable>();
  }

  tables::HeapGraphClassTable* mutable_heap_graph_class_table() {
    return mutable_table<tables::HeapGraphClassTable>();
  }

  const tables::HeapGraphReferenceTable& heap_graph_reference_table() const {
    return table<tables::HeapGraphReferenceTable>();
  }

  tables::HeapGraphReferenceTable* mutable_heap_graph_reference_table() {
    return mutable_table<tables::HeapGraphReferenceTable>();
  }

  const tables::AggregateProfileTable& aggregate_profile_table() const {
    return table<tables::AggregateProfileTable>();
  }

  tables::AggregateProfileTable* mutable_aggregate_profile_table() {
    return mutable_table<tables::AggregateProfileTable>();
  }

  const tables::AggregateSampleTable& aggregate_sample_table() const {
    return table<tables::AggregateSampleTable>();
  }

  tables::AggregateSampleTable* mutable_aggregate_sample_table() {
    return mutable_table<tables::AggregateSampleTable>();
  }

  const tables::VulkanMemoryAllocationsTable& vulkan_memory_allocations_table()
      const {
    return table<tables::VulkanMemoryAllocationsTable>();
  }

  tables::VulkanMemoryAllocationsTable*
  mutable_vulkan_memory_allocations_table() {
    return mutable_table<tables::VulkanMemoryAllocationsTable>();
  }

  const tables::MemorySnapshotTable& memory_snapshot_table() const {
    return table<tables::MemorySnapshotTable>();
  }
  tables::MemorySnapshotTable* mutable_memory_snapshot_table() {
    return mutable_table<tables::MemorySnapshotTable>();
  }

  const tables::ProcessMemorySnapshotTable& process_memory_snapshot_table()
      const {
    return table<tables::ProcessMemorySnapshotTable>();
  }
  tables::ProcessMemorySnapshotTable* mutable_process_memory_snapshot_table() {
    return mutable_table<tables::ProcessMemorySnapshotTable>();
  }

  const tables::MemorySnapshotNodeTable& memory_snapshot_node_table() const {
    return table<tables::MemorySnapshotNodeTable>();
  }
  tables::MemorySnapshotNodeTable* mutable_memory_snapshot_node_table() {
    return mutable_table<tables::MemorySnapshotNodeTable>();
  }

  const tables::MemorySnapshotEdgeTable& memory_snapshot_edge_table() const {
    return table<tables::MemorySnapshotEdgeTable>();
  }
  tables::MemorySnapshotEdgeTable* mutable_memory_snapshot_edge_table() {
    return mutable_table<tables::MemorySnapshotEdgeTable>();
  }

  const tables::AndroidNetworkPacketsTable& android_network_packets_table()
      const {
    return table<tables::AndroidNetworkPacketsTable>();
  }
  tables::AndroidNetworkPacketsTable* mutable_android_network_packets_table() {
    return mutable_table<tables::AndroidNetworkPacketsTable>();
  }

  const tables::V8IsolateTable& v8_isolate_table() const {
    return table<tables::V8IsolateTable>();
  }
  tables::V8IsolateTable* mutable_v8_isolate_table() {
    return mutable_table<tables::V8IsolateTable>();
  }
  const tables::V8JsScriptTable& v8_js_script_table() const {
    return table<tables::V8JsScriptTable>();
  }
  tables::V8JsScriptTable* mutable_v8_js_script_table() {
    return mutable_table<tables::V8JsScriptTable>();
  }
  const tables::V8WasmScriptTable& v8_wasm_script_table() const {
    return table<tables::V8WasmScriptTable>();
  }
  tables::V8WasmScriptTable* mutable_v8_wasm_script_table() {
    return mutable_table<tables::V8WasmScriptTable>();
  }
  const tables::V8JsFunctionTable& v8_js_function_table() const {
    return table<tables::V8JsFunctionTable>();
  }
  tables::V8JsFunctionTable* mutable_v8_js_function_table() {
    return mutable_table<tables::V8JsFunctionTable>();
  }
  const tables::V8JsCodeTable& v8_js_code_table() const {
    return table<tables::V8JsCodeTable>();
  }
  tables::V8JsCodeTable* mutable_v8_js_code_table() {
    return mutable_table<tables::V8JsCodeTable>();
  }
  const tables::V8InternalCodeTable& v8_internal_code_table() const {
    return table<tables::V8InternalCodeTable>();
  }
  tables::V8InternalCodeTable* mutable_v8_internal_code_table() {
    return mutable_table<tables::V8InternalCodeTable>();
  }
  const tables::V8WasmCodeTable& v8_wasm_code_table() const {
    return table<tables::V8WasmCodeTable>();
  }
  tables::V8WasmCodeTable* mutable_v8_wasm_code_table() {
    return mutable_table<tables::V8WasmCodeTable>();
  }
  const tables::V8RegexpCodeTable& v8_regexp_code_table() const {
    return table<tables::V8RegexpCodeTable>();
  }
  tables::V8RegexpCodeTable* mutable_v8_regexp_code_table() {
    return mutable_table<tables::V8RegexpCodeTable>();
  }

  const tables::EtmV4ConfigurationTable& etm_v4_configuration_table() const {
    return table<tables::EtmV4ConfigurationTable>();
  }
  tables::EtmV4ConfigurationTable* mutable_etm_v4_configuration_table() {
    return mutable_table<tables::EtmV4ConfigurationTable>();
  }
  const std::vector<std::unique_ptr<Destructible>>& etm_v4_configuration_data()
      const {
    return etm_v4_configuration_data_;
  }
  std::vector<std::unique_ptr<Destructible>>*
  mutable_etm_v4_configuration_data() {
    return &etm_v4_configuration_data_;
  }
  const tables::EtmV4SessionTable& etm_v4_session_table() const {
    return table<tables::EtmV4SessionTable>();
  }
  tables::EtmV4SessionTable* mutable_etm_v4_session_table() {
    return mutable_table<tables::EtmV4SessionTable>();
  }
  const tables::EtmV4ChunkTable& etm_v4_chunk_table() const {
    return table<tables::EtmV4ChunkTable>();
  }
  tables::EtmV4ChunkTable* mutable_etm_v4_chunk_table() {
    return mutable_table<tables::EtmV4ChunkTable>();
  }
  const std::vector<TraceBlobView>& etm_v4_chunk_data() const {
    return etm_v4_chunk_data_;
  }
  std::vector<TraceBlobView>* mutable_etm_v4_chunk_data() {
    return &etm_v4_chunk_data_;
  }
  const tables::FileTable& file_table() const {
    return table<tables::FileTable>();
  }
  tables::FileTable* mutable_file_table() {
    return mutable_table<tables::FileTable>();
  }
  const tables::ElfFileTable& elf_file_table() const {
    return table<tables::ElfFileTable>();
  }
  tables::ElfFileTable* mutable_elf_file_table() {
    return mutable_table<tables::ElfFileTable>();
  }

  const tables::JitCodeTable& jit_code_table() const {
    return table<tables::JitCodeTable>();
  }
  tables::JitCodeTable* mutable_jit_code_table() {
    return mutable_table<tables::JitCodeTable>();
  }

  const tables::JitFrameTable& jit_frame_table() const {
    return table<tables::JitFrameTable>();
  }
  tables::JitFrameTable* mutable_jit_frame_table() {
    return mutable_table<tables::JitFrameTable>();
  }

  tables::MmapRecordTable* mutable_mmap_record_table() {
    return mutable_table<tables::MmapRecordTable>();
  }
  const tables::MmapRecordTable& mmap_record_table() const {
    return table<tables::MmapRecordTable>();
  }
  const tables::SpeRecordTable& spe_record_table() const {
    return table<tables::SpeRecordTable>();
  }
  tables::SpeRecordTable* mutable_spe_record_table() {
    return mutable_table<tables::SpeRecordTable>();
  }

  const tables::InputMethodClientsTable& inputmethod_clients_table() const {
    return table<tables::InputMethodClientsTable>();
  }
  tables::InputMethodClientsTable* mutable_inputmethod_clients_table() {
    return mutable_table<tables::InputMethodClientsTable>();
  }

  const tables::InputMethodManagerServiceTable&
  inputmethod_manager_service_table() const {
    return table<tables::InputMethodManagerServiceTable>();
  }
  tables::InputMethodManagerServiceTable*
  mutable_inputmethod_manager_service_table() {
    return mutable_table<tables::InputMethodManagerServiceTable>();
  }

  const tables::InputMethodServiceTable& inputmethod_service_table() const {
    return table<tables::InputMethodServiceTable>();
  }
  tables::InputMethodServiceTable* mutable_inputmethod_service_table() {
    return mutable_table<tables::InputMethodServiceTable>();
  }

  const tables::SurfaceFlingerLayersSnapshotTable&
  surfaceflinger_layers_snapshot_table() const {
    return table<tables::SurfaceFlingerLayersSnapshotTable>();
  }
  tables::SurfaceFlingerLayersSnapshotTable*
  mutable_surfaceflinger_layers_snapshot_table() {
    return mutable_table<tables::SurfaceFlingerLayersSnapshotTable>();
  }

  const tables::SurfaceFlingerDisplayTable& surfaceflinger_display_table()
      const {
    return table<tables::SurfaceFlingerDisplayTable>();
  }
  tables::SurfaceFlingerDisplayTable* mutable_surfaceflinger_display_table() {
    return mutable_table<tables::SurfaceFlingerDisplayTable>();
  }

  const tables::SurfaceFlingerLayerTable& surfaceflinger_layer_table() const {
    return table<tables::SurfaceFlingerLayerTable>();
  }
  tables::SurfaceFlingerLayerTable* mutable_surfaceflinger_layer_table() {
    return mutable_table<tables::SurfaceFlingerLayerTable>();
  }

  const tables::SurfaceFlingerTransactionsTable&
  surfaceflinger_transactions_table() const {
    return table<tables::SurfaceFlingerTransactionsTable>();
  }
  tables::SurfaceFlingerTransactionsTable*
  mutable_surfaceflinger_transactions_table() {
    return mutable_table<tables::SurfaceFlingerTransactionsTable>();
  }

  const tables::SurfaceFlingerTransactionTable&
  surfaceflinger_transaction_table() const {
    return table<tables::SurfaceFlingerTransactionTable>();
  }
  tables::SurfaceFlingerTransactionTable*
  mutable_surfaceflinger_transaction_table() {
    return mutable_table<tables::SurfaceFlingerTransactionTable>();
  }

  const tables::SurfaceFlingerTransactionFlagTable&
  surfaceflinger_transaction_flag_table() const {
    return table<tables::SurfaceFlingerTransactionFlagTable>();
  }
  tables::SurfaceFlingerTransactionFlagTable*
  mutable_surfaceflinger_transaction_flag_table() {
    return mutable_table<tables::SurfaceFlingerTransactionFlagTable>();
  }

  const tables::ViewCaptureTable& viewcapture_table() const {
    return table<tables::ViewCaptureTable>();
  }
  tables::ViewCaptureTable* mutable_viewcapture_table() {
    return mutable_table<tables::ViewCaptureTable>();
  }

  const tables::ViewCaptureViewTable& viewcapture_view_table() const {
    return table<tables::ViewCaptureViewTable>();
  }
  tables::ViewCaptureViewTable* mutable_viewcapture_view_table() {
    return mutable_table<tables::ViewCaptureViewTable>();
  }

  const tables::ViewCaptureInternedDataTable& viewcapture_interned_data_table()
      const {
    return table<tables::ViewCaptureInternedDataTable>();
  }
  tables::ViewCaptureInternedDataTable*
  mutable_viewcapture_interned_data_table() {
    return mutable_table<tables::ViewCaptureInternedDataTable>();
  }

  const tables::WindowManagerTable& windowmanager_table() const {
    return table<tables::WindowManagerTable>();
  }
  tables::WindowManagerTable* mutable_windowmanager_table() {
    return mutable_table<tables::WindowManagerTable>();
  }

  const tables::WindowManagerWindowContainerTable&
  windowmanager_windowcontainer_table() const {
    return table<tables::WindowManagerWindowContainerTable>();
  }
  tables::WindowManagerWindowContainerTable*
  mutable_windowmanager_windowcontainer_table() {
    return mutable_table<tables::WindowManagerWindowContainerTable>();
  }

  const tables::WindowManagerShellTransitionsTable&
  window_manager_shell_transitions_table() const {
    return table<tables::WindowManagerShellTransitionsTable>();
  }
  tables::WindowManagerShellTransitionsTable*
  mutable_window_manager_shell_transitions_table() {
    return mutable_table<tables::WindowManagerShellTransitionsTable>();
  }

  const tables::WindowManagerShellTransitionHandlersTable&
  window_manager_shell_transition_handlers_table() const {
    return table<tables::WindowManagerShellTransitionHandlersTable>();
  }
  tables::WindowManagerShellTransitionHandlersTable*
  mutable_window_manager_shell_transition_handlers_table() {
    return mutable_table<tables::WindowManagerShellTransitionHandlersTable>();
  }

  const tables::WindowManagerShellTransitionParticipantsTable&
  window_manager_shell_transition_participants_table() const {
    return table<tables::WindowManagerShellTransitionParticipantsTable>();
  }
  tables::WindowManagerShellTransitionParticipantsTable*
  mutable_window_manager_shell_transition_participants_table() {
    return mutable_table<
        tables::WindowManagerShellTransitionParticipantsTable>();
  }

  const tables::WindowManagerShellTransitionProtosTable&
  window_manager_shell_transition_protos_table() const {
    return table<tables::WindowManagerShellTransitionProtosTable>();
  }
  tables::WindowManagerShellTransitionProtosTable*
  mutable_window_manager_shell_transition_protos_table() {
    return mutable_table<tables::WindowManagerShellTransitionProtosTable>();
  }

  const tables::ProtoLogTable& protolog_table() const {
    return table<tables::ProtoLogTable>();
  }
  tables::ProtoLogTable* mutable_protolog_table() {
    return mutable_table<tables::ProtoLogTable>();
  }

  const tables::WinscopeTraceRectTable& winscope_trace_rect_table() const {
    return table<tables::WinscopeTraceRectTable>();
  }
  tables::WinscopeTraceRectTable* mutable_winscope_trace_rect_table() {
    return mutable_table<tables::WinscopeTraceRectTable>();
  }

  const tables::WinscopeRectTable& winscope_rect_table() const {
    return table<tables::WinscopeRectTable>();
  }
  tables::WinscopeRectTable* mutable_winscope_rect_table() {
    return mutable_table<tables::WinscopeRectTable>();
  }

  const tables::WinscopeFillRegionTable& winscope_fill_region_table() const {
    return table<tables::WinscopeFillRegionTable>();
  }
  tables::WinscopeFillRegionTable* mutable_winscope_fill_region_table() {
    return mutable_table<tables::WinscopeFillRegionTable>();
  }

  const tables::WinscopeTransformTable& winscope_transform_table() const {
    return table<tables::WinscopeTransformTable>();
  }
  tables::WinscopeTransformTable* mutable_winscope_transform_table() {
    return mutable_table<tables::WinscopeTransformTable>();
  }

  const tables::ExperimentalProtoPathTable& experimental_proto_path_table()
      const {
    return table<tables::ExperimentalProtoPathTable>();
  }
  tables::ExperimentalProtoPathTable* mutable_experimental_proto_path_table() {
    return mutable_table<tables::ExperimentalProtoPathTable>();
  }

  const tables::ExperimentalProtoContentTable&
  experimental_proto_content_table() const {
    return table<tables::ExperimentalProtoContentTable>();
  }
  tables::ExperimentalProtoContentTable*
  mutable_experimental_proto_content_table() {
    return mutable_table<tables::ExperimentalProtoContentTable>();
  }

  const tables::ExpMissingChromeProcTable&
  experimental_missing_chrome_processes_table() const {
    return table<tables::ExpMissingChromeProcTable>();
  }
  tables::ExpMissingChromeProcTable*
  mutable_experimental_missing_chrome_processes_table() {
    return mutable_table<tables::ExpMissingChromeProcTable>();
  }

  const StringPool& string_pool() const { return string_pool_; }
  StringPool* mutable_string_pool() { return &string_pool_; }

  // Number of interned strings in the pool. Includes the empty string w/ ID=0.
  size_t string_count() const { return string_pool_.size(); }

  StringId GetIdForVariadicType(Variadic::Type type) const {
    return variadic_type_ids_[type];
  }

  std::optional<Variadic::Type> GetVariadicTypeForId(StringId id) const {
    auto it =
        std::find(variadic_type_ids_.begin(), variadic_type_ids_.end(), id);
    if (it == variadic_type_ids_.end())
      return std::nullopt;

    int64_t idx = std::distance(variadic_type_ids_.begin(), it);
    return static_cast<Variadic::Type>(idx);
  }

 private:
  using StringHash = uint64_t;

  TraceStorage(const TraceStorage&) = delete;
  TraceStorage& operator=(const TraceStorage&) = delete;

  TraceStorage(TraceStorage&&) = delete;
  TraceStorage& operator=(TraceStorage&&) = delete;

  friend etm::TargetMemory;
  Destructible* etm_target_memory() { return etm_target_memory_.get(); }
  void set_etm_target_memory(std::unique_ptr<Destructible> target_memory) {
    etm_target_memory_ = std::move(target_memory);
  }

  // Helper to get a table by type.
  template <typename T>
  T* mutable_table() {
    constexpr size_t idx = base::variant_index<tables::AllTables, T>();
    return reinterpret_cast<T*>(
        &tables_storage_[idx * sizeof(dataframe::Dataframe)]);
  }
  template <typename T>
  const T& table() const {
    constexpr size_t idx = base::variant_index<tables::AllTables, T>();
    return *reinterpret_cast<const T*>(
        &tables_storage_[idx * sizeof(dataframe::Dataframe)]);
  }

  // One entry for each unique string in the trace.
  StringPool string_pool_;

  // Stats about parsing the trace.
  StatsMap stats_{};
  VirtualTrackSlices virtual_track_slices_;
  SqlStats sql_stats_;

  // ETM tables
  // Indexed by tables::EtmV4ConfigurationTable::Id
  std::vector<std::unique_ptr<Destructible>> etm_v4_configuration_data_;
  // Indexed by tables::EtmV4TraceTable::Id
  std::vector<TraceBlobView> etm_v4_chunk_data_;
  std::unique_ptr<Destructible> etm_target_memory_;

  // Aligned storage for all table dataframes.
  alignas(
      dataframe::Dataframe) char tables_storage_[tables::kTableCount *
                                                 sizeof(dataframe::Dataframe)];

  // The below array allow us to map between enums and their string
  // representations.
  std::array<StringId, Variadic::kMaxType + 1> variadic_type_ids_;
};

}  // namespace perfetto::trace_processor

template <>
struct std::hash<::perfetto::trace_processor::BaseId> {
  using argument_type = ::perfetto::trace_processor::BaseId;
  using result_type = size_t;

  result_type operator()(const argument_type& r) const {
    return std::hash<uint32_t>{}(r.value);
  }
};

template <>
struct std::hash<::perfetto::trace_processor::SliceId>
    : std::hash<::perfetto::trace_processor::BaseId> {};
template <>
struct std::hash<::perfetto::trace_processor::TrackId>
    : std::hash<::perfetto::trace_processor::BaseId> {};
template <>
struct std::hash<::perfetto::trace_processor::MappingId>
    : std::hash<::perfetto::trace_processor::BaseId> {};
template <>
struct std::hash<::perfetto::trace_processor::CallsiteId>
    : std::hash<::perfetto::trace_processor::BaseId> {};
template <>
struct std::hash<::perfetto::trace_processor::FrameId>
    : std::hash<::perfetto::trace_processor::BaseId> {};
template <>
struct std::hash<::perfetto::trace_processor::tables::HeapGraphObjectTable_Id>
    : std::hash<::perfetto::trace_processor::BaseId> {};
template <>
struct std::hash<::perfetto::trace_processor::tables::V8IsolateTable_Id>
    : std::hash<::perfetto::trace_processor::BaseId> {};
template <>
struct std::hash<::perfetto::trace_processor::tables::JitCodeTable_Id>
    : std::hash<::perfetto::trace_processor::BaseId> {};

#endif  // SRC_TRACE_PROCESSOR_STORAGE_TRACE_STORAGE_H_
