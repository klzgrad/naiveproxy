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
#include <tuple>

#include "perfetto/ext/base/flat_hash_map.h"
#include "perfetto/ext/base/small_vector.h"
#include "src/trace_processor/dataframe/dataframe.h"
#include "src/trace_processor/importers/common/global_args_tracker.h"
#include "src/trace_processor/storage/trace_storage.h"
#include "src/trace_processor/tables/android_tables_py.h"
#include "src/trace_processor/tables/flow_tables_py.h"
#include "src/trace_processor/tables/memory_tables_py.h"
#include "src/trace_processor/tables/metadata_tables_py.h"
#include "src/trace_processor/tables/trace_proto_tables_py.h"
#include "src/trace_processor/tables/track_tables_py.h"
#include "src/trace_processor/tables/winscope_tables_py.h"
#include "src/trace_processor/types/trace_processor_context.h"
#include "src/trace_processor/types/variadic.h"

namespace perfetto::trace_processor {

// Tracks and stores args for rows until the end of the packet. This allows
// allows args to pushed as a group into storage.
class ArgsTracker {
 public:
  using UpdatePolicy = GlobalArgsTracker::UpdatePolicy;
  using CompactArg = GlobalArgsTracker::CompactArg;
  using CompactArgSet = base::SmallVector<CompactArg, 16>;

  // Stores the table and row at creation time which args are associated with.
  // This allows callers to directly add args without repeating the row the
  // args should be associated with.
  class BoundInserter {
   public:
    virtual ~BoundInserter();

    BoundInserter(BoundInserter&&) noexcept = default;
    BoundInserter& operator=(BoundInserter&&) noexcept = default;

    BoundInserter(const BoundInserter&) = delete;
    BoundInserter& operator=(const BoundInserter&) = delete;

    // Adds an arg with the same key and flat_key.
    BoundInserter& AddArg(
        StringId key,
        Variadic v,
        UpdatePolicy update_policy = UpdatePolicy::kAddOrUpdate) {
      return AddArg(key, key, v, update_policy);
    }

    virtual BoundInserter& AddArg(
        StringId flat_key,
        StringId key,
        Variadic v,
        UpdatePolicy update_policy = UpdatePolicy::kAddOrUpdate) {
      args_tracker_->AddArg(ptr_, col_, row_, flat_key, key, v, update_policy);
      return *this;
    }

    // IncrementArrayEntryIndex() and GetNextArrayEntryIndex() provide a way to
    // track the next array index for an array under a specific key.
    size_t GetNextArrayEntryIndex(StringId key) {
      // Zero-initializes |key| in the map if it doesn't exist yet.
      return args_tracker_
          ->array_indexes_[std::make_tuple(ptr_, col_, row_, key)];
    }

    // Returns the next available array index after increment.
    size_t IncrementArrayEntryIndex(StringId key) {
      // Zero-initializes |key| in the map if it doesn't exist yet.
      return ++args_tracker_
                   ->array_indexes_[std::make_tuple(ptr_, col_, row_, key)];
    }

   protected:
    BoundInserter(ArgsTracker* args_tracker,
                  dataframe::Dataframe* dataframe,
                  uint32_t col,
                  uint32_t row);

   private:
    friend class ArgsTracker;

    ArgsTracker* args_tracker_ = nullptr;
    void* ptr_ = nullptr;
    uint32_t col_ = 0;
    uint32_t row_ = 0;
  };

  explicit ArgsTracker(TraceProcessorContext*);

  ArgsTracker(const ArgsTracker&) = delete;
  ArgsTracker& operator=(const ArgsTracker&) = delete;

  ArgsTracker(ArgsTracker&&) = default;
  ArgsTracker& operator=(ArgsTracker&&) = default;

  virtual ~ArgsTracker();

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
    uint32_t row = table->FindById(id)->ToRowNumber().row_number();
    return BoundInserter(this, &table->dataframe(),
                         tables::MetadataTable::ColumnIndex::int_value, row);
  }

  BoundInserter AddArgsTo(TrackId id) {
    auto* table = context_->storage->mutable_track_table();
    uint32_t row = table->FindById(id)->ToRowNumber().row_number();
    return BoundInserter(this, &table->dataframe(),
                         tables::TrackTable::ColumnIndex::source_arg_set_id,
                         row);
  }

  BoundInserter AddArgsTo(VulkanAllocId id) {
    return AddArgsTo(
        context_->storage->mutable_vulkan_memory_allocations_table(), id);
  }

  BoundInserter AddArgsTo(UniquePid id) {
    auto* table = context_->storage->mutable_process_table();
    return BoundInserter(this, &table->dataframe(),
                         tables::ProcessTable::ColumnIndex::arg_set_id, id);
  }

  BoundInserter AddArgsTo(tables::ExperimentalProtoPathTable::Id id) {
    return AddArgsTo(context_->storage->mutable_experimental_proto_path_table(),
                     id);
  }

  BoundInserter AddArgsTo(tables::CpuTable::Id id) {
    return AddArgsTo(context_->storage->mutable_cpu_table(), id);
  }

  // Returns a CompactArgSet which contains the args inserted into this
  // ArgsTracker. Requires that every arg in this tracker was inserted for the
  // "arg_set_id" column given by |column| at the given |row_number|.
  //
  // Note that this means the args stored in this tracker will *not* be flushed
  // into the tables: it is the callers responsibility to ensure this happens if
  // necessary.
  CompactArgSet ToCompactArgSet(const dataframe::Dataframe&,
                                uint32_t column,
                                uint32_t row_number) &&;

  // Returns whether this ArgsTracker contains any arg which require translation
  // according to the provided |table|.
  bool NeedsTranslation(const ArgsTranslationTable& table) const;

  // Commits the added args to storage.
  // Virtual for testing.
  virtual void Flush();

 private:
  template <typename T>
  BoundInserter AddArgsTo(T* table, typename T::Id id) {
    uint32_t row = table->FindById(id)->ToRowNumber().row_number();
    return BoundInserter(this, &table->dataframe(), T::ColumnIndex::arg_set_id,
                         row);
  }

  void AddArg(void* ptr,
              uint32_t col,
              uint32_t row,
              StringId flat_key,
              StringId key,
              Variadic,
              UpdatePolicy);

  base::SmallVector<GlobalArgsTracker::Arg, 16> args_;
  TraceProcessorContext* context_ = nullptr;

  using ArrayKeyTuple = std::tuple<void* /*ptr*/,
                                   uint32_t /*col*/,
                                   uint32_t /*row*/,
                                   StringId /*key*/>;
  struct Hasher {
    uint64_t operator()(const ArrayKeyTuple& t) const {
      return base::FnvHasher::Combine(
          reinterpret_cast<uint64_t>(std::get<0>(t)), std::get<1>(t),
          std::get<2>(t), std::get<3>(t).raw_id());
    }
  };
  base::FlatHashMap<ArrayKeyTuple, size_t /*next_index*/, Hasher>
      array_indexes_;
};

}  // namespace perfetto::trace_processor

#endif  // SRC_TRACE_PROCESSOR_IMPORTERS_COMMON_ARGS_TRACKER_H_
