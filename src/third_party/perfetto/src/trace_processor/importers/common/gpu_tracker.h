/*
 * Copyright (C) 2024 The Android Open Source Project
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

#ifndef SRC_TRACE_PROCESSOR_IMPORTERS_COMMON_GPU_TRACKER_H_
#define SRC_TRACE_PROCESSOR_IMPORTERS_COMMON_GPU_TRACKER_H_

#include <cstdint>
#include <string_view>
#include <vector>

#include "perfetto/ext/base/flat_hash_map.h"
#include "src/trace_processor/storage/trace_storage.h"
#include "src/trace_processor/tables/metadata_tables_py.h"
#include "src/trace_processor/types/trace_processor_context.h"

namespace perfetto::trace_processor {

class TraceProcessorContext;

class GpuTracker {
 public:
  explicit GpuTracker(TraceProcessorContext*);

  // Ensures the given gpu number exists in the GPU table for this machine,
  // creating a new row if this is the first time this gpu number has been seen.
  tables::GpuTable::Id GetOrCreateGpu(uint32_t gpu);

  // Sets or updates the metadata for the specified GPU in the GpuTable.
  tables::GpuTable::Id SetGpuInfo(uint32_t gpu,
                                  std::string_view name,
                                  std::string_view vendor,
                                  std::string_view model,
                                  std::string_view architecture,
                                  std::string_view uuid,
                                  std::string_view pci_bdf);

  // Registers a GPU render stage slice. Correlates it with any pending
  // event_wait_ids that reference this event_id.
  void AddGpuRenderStageSlice(uint64_t event_id, SliceId slice_id);

  // Registers a track event slice as a submission point. Correlates it with
  // all GPU render stage slices (past and future) that share the same
  // event_id.
  void AddRenderStageSubmission(uint64_t event_id, SliceId slice_id);

  // Registers a track event slice as waiting on a GPU render stage event
  // to complete. Correlates all GPU render stage slices (past and future)
  // that share the same event_id with this slice.
  void AddRenderStageWait(uint64_t event_id, SliceId slice_id);

  // Registers that a GPU render stage slice waited on another event.
  // Correlates the referenced event_id with this slice.
  void AddEventWait(uint64_t waited_event_id, SliceId slice_id);

 private:
  TraceProcessorContext* const context_;

  // Maps gpu number to GpuTable::Id for the current machine.
  base::FlatHashMap<uint32_t, tables::GpuTable::Id> gpu_ids_;

  // Maps event_id to GPU render stage slice IDs.
  base::FlatHashMap<uint64_t, std::vector<SliceId>> event_id_to_gpu_slices_;

  // Maps event_id to the track event slice ID.
  base::FlatHashMap<uint64_t, SliceId> event_id_to_track_event_slice_;

  // Maps event_id to the terminating track event slice ID.
  base::FlatHashMap<uint64_t, SliceId> event_id_to_terminating_slice_;

  // Maps event_id to slice IDs that are waiting on it (via event_wait_ids).
  base::FlatHashMap<uint64_t, std::vector<SliceId>> event_id_to_waiting_slices_;
};

}  // namespace perfetto::trace_processor

#endif  // SRC_TRACE_PROCESSOR_IMPORTERS_COMMON_GPU_TRACKER_H_
