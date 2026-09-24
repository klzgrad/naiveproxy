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

#ifndef SRC_TRACE_PROCESSOR_IMPORTERS_COMMON_ARGS_TRACKER_H_
#define SRC_TRACE_PROCESSOR_IMPORTERS_COMMON_ARGS_TRACKER_H_

#include <cstddef>
#include <cstdint>

#include "perfetto/ext/base/small_vector.h"
#include "src/trace_processor/core/dataframe/dataframe.h"
#include "src/trace_processor/importers/common/global_args_tracker.h"
#include "src/trace_processor/storage/trace_storage.h"
#include "src/trace_processor/tables/android_tables_py.h"
#include "src/trace_processor/tables/counter_tables_py.h"
#include "src/trace_processor/tables/flow_tables_py.h"
#include "src/trace_processor/tables/log_tables_py.h"
#include "src/trace_processor/tables/memory_tables_py.h"
#include "src/trace_processor/tables/metadata_tables_py.h"
#include "src/trace_processor/tables/profiler_tables_py.h"
#include "src/trace_processor/tables/slice_tables_py.h"
#include "src/trace_processor/tables/state_tables_py.h"
#include "src/trace_processor/tables/trace_proto_tables_py.h"
#include "src/trace_processor/tables/track_tables_py.h"
#include "src/trace_processor/tables/winscope_tables_py.h"
#include "src/trace_processor/types/trace_processor_context.h"
#include "src/trace_processor/types/variadic.h"

namespace perfetto::trace_processor {

class ArgsTranslationTable;

// Accumulates the args for a single target cell (the dataframe column+row that
// holds an arg_set_id) and commits them as one arg set when it goes out of
// scope. Obtain one from ArgsTracker::AddArgsTo*; callers needing several cells
// acquire one inserter per cell. Same-key duplicates are collapsed in place
// (kSkipIfExists keeps the first value, kAddOrUpdate the last).
//
// Move-only; a moved-from inserter owns nothing and commits nothing. Its buffer
// is borrowed from a pool in GlobalArgsTracker to avoid per-inserter
// allocation.
class ArgsInserter {
 public:
  using UpdatePolicy = GlobalArgsTracker::UpdatePolicy;
  using CompactArg = GlobalArgsTracker::CompactArg;
  using CompactArgSet = base::SmallVector<CompactArg, 16>;

  // Constructs an empty inserter that owns no buffer and commits nothing. Used
  // for the moved-from state and for default-constructed (empty) map entries.
  ArgsInserter() = default;

  ~ArgsInserter();

  ArgsInserter(ArgsInserter&&) noexcept;
  ArgsInserter& operator=(ArgsInserter&&) noexcept;

  ArgsInserter(const ArgsInserter&) = delete;
  ArgsInserter& operator=(const ArgsInserter&) = delete;

  // Adds an arg with the same key and flat_key.
  ArgsInserter& AddArg(
      StringId key,
      Variadic v,
      UpdatePolicy update_policy = UpdatePolicy::kAddOrUpdate) {
    return AddArg(key, key, v, update_policy);
  }

  ArgsInserter& AddArg(StringId flat_key,
                       StringId key,
                       Variadic v,
                       UpdatePolicy update_policy = UpdatePolicy::kAddOrUpdate);

  // IncrementArrayEntryIndex() and GetNextArrayEntryIndex() provide a way to
  // track the next array index for an array under a specific key.
  size_t GetNextArrayEntryIndex(StringId key) {
    // Zero-initializes |key| in the map if it doesn't exist yet.
    return buffer_->array_indexes[key];
  }

  // Returns the next available array index after increment.
  size_t IncrementArrayEntryIndex(StringId key) {
    // Zero-initializes |key| in the map if it doesn't exist yet.
    return ++buffer_->array_indexes[key];
  }

  // The id of the row these args are being added to (the value of the typed
  // Id passed to ArgsTracker::AddArgsTo).
  uint32_t id() const { return id_; }

  // Returns whether this inserter holds any arg which requires translation
  // according to the provided |table|.
  bool NeedsTranslation(const ArgsTranslationTable& table) const;

  // Moves the accumulated args out into a CompactArgSet, leaving this inserter
  // empty so it commits nothing when destroyed. Used by callers that must
  // post-process the args (e.g. translation) before they reach storage.
  CompactArgSet ToCompactArgSet() &&;

 private:
  friend class ArgsTracker;

  ArgsInserter(GlobalArgsTracker* global,
               dataframe::Dataframe* dataframe,
               uint32_t col,
               uint32_t row,
               uint32_t id);

  // Commits the accumulated args (if any) as a single arg set and writes the
  // resulting arg_set_id into the target cell.
  void Commit();

  // Non-null iff this inserter owns a pooled buffer; both are nulled on move.
  GlobalArgsTracker* global_ = nullptr;
  GlobalArgsTracker::ArgsBuffer* buffer_ = nullptr;
  dataframe::Dataframe* df_ = nullptr;
  uint32_t col_ = 0;
  uint32_t row_ = 0;
  uint32_t id_ = 0;
};

// Factory that resolves a table row id to the dataframe cell holding its
// arg_set_id and hands back an ArgsInserter bound to that cell. Stateless
// beyond the context pointer, so constructing one is free.
class ArgsTracker {
 public:
  using UpdatePolicy = GlobalArgsTracker::UpdatePolicy;
  using CompactArg = GlobalArgsTracker::CompactArg;
  using CompactArgSet = ArgsInserter::CompactArgSet;
  // The bound inserter is now a standalone ArgsInserter; this alias keeps
  // existing references compiling.
  using BoundInserter = ArgsInserter;

  explicit ArgsTracker(TraceProcessorContext* context) : context_(context) {}

  ArgsTracker(const ArgsTracker&) = delete;
  ArgsTracker& operator=(const ArgsTracker&) = delete;

  ArgsTracker(ArgsTracker&&) = default;
  ArgsTracker& operator=(ArgsTracker&&) = default;

  ~ArgsTracker() = default;

  BoundInserter AddArgsTo(tables::ChromeRawTable::Id id) {
    return AddArgsTo(context_->storage->mutable_chrome_raw_table(), id);
  }

  BoundInserter AddArgsTo(tables::FtraceEventTable::Id id) {
    return AddArgsTo(context_->storage->mutable_ftrace_event_table(), id);
  }

  BoundInserter AddArgsTo(CounterId id) {
    return AddArgsTo(context_->storage->mutable_counter_table(), id);
  }

  BoundInserter AddArgsTo(SliceId id) {
    return AddArgsTo(context_->storage->mutable_slice_table(), id);
  }

  BoundInserter AddArgsTo(tables::StateTable::Id id) {
    return AddArgsTo(context_->storage->mutable_state_table(), id);
  }

  BoundInserter AddArgsTo(tables::FlowTable::Id id) {
    return AddArgsTo(context_->storage->mutable_flow_table(), id);
  }

  BoundInserter AddArgsTo(tables::InputMethodClientsTable::Id id) {
    return AddArgsTo(context_->storage->mutable_inputmethod_clients_table(),
                     id);
  }

  BoundInserter AddArgsTo(tables::InputMethodServiceTable::Id id) {
    return AddArgsTo(context_->storage->mutable_inputmethod_service_table(),
                     id);
  }

  BoundInserter AddArgsTo(tables::InputMethodManagerServiceTable::Id id) {
    return AddArgsTo(
        context_->storage->mutable_inputmethod_manager_service_table(), id);
  }

  BoundInserter AddArgsTo(tables::MemorySnapshotNodeTable::Id id) {
    return AddArgsTo(context_->storage->mutable_memory_snapshot_node_table(),
                     id);
  }

  BoundInserter AddArgsTo(tables::SurfaceFlingerLayersSnapshotTable::Id id) {
    return AddArgsTo(
        context_->storage->mutable_surfaceflinger_layers_snapshot_table(), id);
  }

  BoundInserter AddArgsTo(tables::SurfaceFlingerLayerTable::Id id) {
    return AddArgsTo(context_->storage->mutable_surfaceflinger_layer_table(),
                     id);
  }

  BoundInserter AddArgsTo(tables::SurfaceFlingerTransactionsTable::Id id) {
    return AddArgsTo(
        context_->storage->mutable_surfaceflinger_transactions_table(), id);
  }

  BoundInserter AddArgsTo(tables::SurfaceFlingerTransactionTable::Id id) {
    return AddArgsTo(
        context_->storage->mutable_surfaceflinger_transaction_table(), id);
  }

  BoundInserter AddArgsTo(tables::ViewCaptureTable::Id id) {
    return AddArgsTo(context_->storage->mutable_viewcapture_table(), id);
  }

  BoundInserter AddArgsTo(tables::ViewCaptureViewTable::Id id) {
    return AddArgsTo(context_->storage->mutable_viewcapture_view_table(), id);
  }

  BoundInserter AddArgsTo(tables::WindowManagerTable::Id id) {
    return AddArgsTo(context_->storage->mutable_windowmanager_table(), id);
  }

  BoundInserter AddArgsTo(tables::WindowManagerWindowContainerTable::Id id) {
    return AddArgsTo(
        context_->storage->mutable_windowmanager_windowcontainer_table(), id);
  }

  BoundInserter AddArgsTo(tables::WindowManagerShellTransitionsTable::Id id) {
    return AddArgsTo(
        context_->storage->mutable_window_manager_shell_transitions_table(),
        id);
  }

  BoundInserter AddArgsTo(tables::AndroidKeyEventsTable::Id id) {
    return AddArgsTo(context_->storage->mutable_android_key_events_table(), id);
  }

  BoundInserter AddArgsTo(tables::AndroidMotionEventsTable::Id id) {
    return AddArgsTo(context_->storage->mutable_android_motion_events_table(),
                     id);
  }

  BoundInserter AddArgsTo(tables::AndroidInputEventDispatchTable::Id id) {
    return AddArgsTo(
        context_->storage->mutable_android_input_event_dispatch_table(), id);
  }

  BoundInserter AddArgsTo(MetadataId id) {
    auto* table = context_->storage->mutable_metadata_table();
    uint32_t row = (*table)[id].ToRowNumber().row_number();
    return Bind(&table->dataframe(),
                tables::MetadataTable::ColumnIndex::int_value, row, id.value);
  }

  BoundInserter AddArgsTo(TrackId id) {
    auto* table = context_->storage->mutable_track_table();
    uint32_t row = (*table)[id].ToRowNumber().row_number();
    return Bind(&table->dataframe(),
                tables::TrackTable::ColumnIndex::source_arg_set_id, row,
                id.value);
  }

  BoundInserter AddArgsTo(VulkanAllocId id) {
    return AddArgsTo(
        context_->storage->mutable_vulkan_memory_allocations_table(), id);
  }

  BoundInserter AddArgsToProcess(UniquePid id) {
    auto* table = context_->storage->mutable_process_table();
    return Bind(&table->dataframe(),
                tables::ProcessTable::ColumnIndex::arg_set_id, id, id);
  }

  BoundInserter AddArgsToThread(UniqueTid id) {
    auto* table = context_->storage->mutable_thread_table();
    return Bind(&table->dataframe(),
                tables::ThreadTable::ColumnIndex::arg_set_id, id, id);
  }

  BoundInserter AddArgsTo(tables::ExperimentalProtoPathTable::Id id) {
    return AddArgsTo(context_->storage->mutable_experimental_proto_path_table(),
                     id);
  }

  BoundInserter AddArgsTo(tables::CpuTable::Id id) {
    return AddArgsTo(context_->storage->mutable_cpu_table(), id);
  }

  BoundInserter AddArgsTo(tables::GpuTable::Id id) {
    return AddArgsTo(context_->storage->mutable_gpu_table(), id);
  }

  BoundInserter AddArgsTo(tables::TraceImportLogsTable::Id id) {
    return AddArgsTo(context_->storage->mutable_trace_import_logs_table(), id);
  }

  BoundInserter AddArgsTo(tables::LogTable::Id id) {
    return AddArgsTo(context_->storage->mutable_log_table(), id);
  }

 private:
  template <typename T>
  BoundInserter AddArgsTo(T* table, typename T::Id id) {
    uint32_t row = (*table)[id].ToRowNumber().row_number();
    return Bind(&table->dataframe(), T::ColumnIndex::arg_set_id, row, id.value);
  }

  ArgsInserter Bind(dataframe::Dataframe* df,
                    uint32_t col,
                    uint32_t row,
                    uint32_t id) {
    return ArgsInserter(context_->global_args_tracker.get(), df, col, row, id);
  }

  TraceProcessorContext* context_ = nullptr;
};

}  // namespace perfetto::trace_processor

#endif  // SRC_TRACE_PROCESSOR_IMPORTERS_COMMON_ARGS_TRACKER_H_
