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

#include "src/trace_processor/importers/etm/etm_tracker.h"

#include <cstdint>
#include <memory>
#include <utility>
#include <vector>

#include "perfetto/base/flat_set.h"
#include "perfetto/base/status.h"
#include "perfetto/ext/base/flat_hash_map.h"
#include "perfetto/trace_processor/trace_blob_view.h"
#include "src/trace_processor/importers/etm/storage_handle.h"
#include "src/trace_processor/importers/etm/target_memory.h"
#include "src/trace_processor/importers/etm/types.h"
#include "src/trace_processor/importers/etm/util.h"
#include "src/trace_processor/storage/trace_storage.h"
#include "src/trace_processor/tables/etm_tables_py.h"
#include "src/trace_processor/types/trace_processor_context.h"

namespace perfetto::trace_processor::etm {

EtmTracker::EtmTracker(TraceProcessorContext* context) : context_(context) {}
EtmTracker::~EtmTracker() = default;

void EtmTracker::AddSessionData(tables::EtmV4SessionTable::Id session_id,
                                std::vector<TraceBlobView> chunks) {
  uint32_t chunk_set_id = context_->storage->etm_v4_chunk_table().row_count();

  for (auto& chunk : chunks) {
    auto chunk_id = context_->storage->mutable_etm_v4_chunk_table()
                        ->Insert({session_id, chunk_set_id,
                                  static_cast<int64_t>(chunk.size())})
                        .id;
    StorageHandle(context_).StoreChunk(chunk_id, std::move(chunk));
  }
}

base::FlatSet<tables::EtmV4ConfigurationTable::Id>
EtmTracker::InsertEtmV4Config(PerCpuConfiguration per_cpu_configs) {
  base::FlatSet<tables::EtmV4ConfigurationTable::Id> res;
  uint32_t set_id = context_->storage->etm_v4_configuration_table().row_count();
  for (auto it = per_cpu_configs.GetIterator(); it; ++it) {
    uint32_t cpu = it.key();
    std::unique_ptr<Configuration> config = std::move(it.value());
    const EtmV4Config etm_v4_config = config->etm_v4_config();
    tables::EtmV4ConfigurationTable::Row row;
    row.set_id = set_id;

    row.cpu = cpu;
    row.cs_trace_stream_id = etm_v4_config.getTraceID();
    row.core_profile =
        context_->storage->InternString(ToString(etm_v4_config.coreProfile()));
    row.arch_version =
        context_->storage->InternString(ToString(etm_v4_config.archVersion()));

    row.major_version = etm_v4_config.MajVersion();
    row.minor_version = etm_v4_config.MinVersion();

    row.max_speculation_depth = etm_v4_config.MaxSpecDepth();
    row.max_speculation_depth = etm_v4_config.MaxSpecDepth();

    row.bool_flags = 0;
    if (etm_v4_config.hasCycleCountI()) {
      row.bool_flags |= ETM_V4_CONFIGURATION_TABLE_FLAG_HAS_CYCLE_COUNT;
    }
    if (etm_v4_config.enabledTS()) {
      row.bool_flags |= ETM_V4_CONFIGURATION_TABLE_FLAG_TS_ENABLED;
    }

    auto id =
        context_->storage->mutable_etm_v4_configuration_table()->Insert(row).id;
    res.insert(id);
    StorageHandle(context_).StoreEtmV4Config(id, std::move(config));
  }
  return res;
}

base::Status EtmTracker::Finalize() {
  TargetMemory::InitStorage(context_);
  return base::OkStatus();
}

}  // namespace perfetto::trace_processor::etm
