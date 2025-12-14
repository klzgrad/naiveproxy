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

#include "src/trace_processor/importers/etm/storage_handle.h"
#include <opencsd/etmv4/trc_cmp_cfg_etmv4.h>

#include "perfetto/base/logging.h"
#include "perfetto/trace_processor/trace_blob_view.h"
#include "src/trace_processor/importers/etm/types.h"
#include "src/trace_processor/storage/trace_storage.h"

namespace perfetto::trace_processor::etm {

void StorageHandle::StoreEtmV4Config(tables::EtmV4ConfigurationTable::Id id,
                                     std::unique_ptr<Configuration> config) {
  PERFETTO_CHECK(id.value == storage_->etm_v4_configuration_data().size());
  storage_->mutable_etm_v4_configuration_data()->push_back(std::move(config));
}

const Configuration& StorageHandle::GetEtmV4Config(
    tables::EtmV4ConfigurationTable::Id id) const {
  PERFETTO_CHECK(id.value < storage_->etm_v4_configuration_data().size());
  return *static_cast<const Configuration*>(
      storage_->etm_v4_configuration_data()[id.value].get());
}

void StorageHandle::StoreChunk(tables::EtmV4ChunkTable::Id id,
                               TraceBlobView chunk) {
  PERFETTO_CHECK(id.value == storage_->etm_v4_chunk_data().size());
  storage_->mutable_etm_v4_chunk_data()->push_back(std::move(chunk));
}

const TraceBlobView& StorageHandle::GetChunk(
    tables::EtmV4ChunkTable::Id id) const {
  PERFETTO_CHECK(id.value < storage_->etm_v4_chunk_data().size());
  return storage_->etm_v4_chunk_data()[id.value];
}

}  // namespace perfetto::trace_processor::etm
