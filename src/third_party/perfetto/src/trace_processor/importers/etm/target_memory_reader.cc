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

#include "src/trace_processor/importers/etm/target_memory_reader.h"

#include <cstddef>
#include <cstdint>

#include "perfetto/base/logging.h"
#include "src/trace_processor/importers/common/address_range.h"
#include "src/trace_processor/importers/etm/mapping_version.h"
#include "src/trace_processor/importers/etm/opencsd.h"
#include "src/trace_processor/importers/etm/target_memory.h"

namespace perfetto::trace_processor::etm {
namespace {
uint32_t Read(const MappingVersion& mapping,
              const AddressRange& range,
              uint8_t* dest) {
  if (!mapping.data()) {
    return 0;
  }
  memcpy(dest, mapping.data() + (range.start() - mapping.start()),
         range.size());
  return static_cast<uint32_t>(range.size());
}
}  // namespace

ocsd_err_t TargetMemoryReader::ReadTargetMemory(
    const ocsd_vaddr_t address,
    const uint8_t,
    const ocsd_mem_space_acc_t mem_space,
    uint32_t* num_bytes,
    uint8_t* dest) {
  if (mem_space != OCSD_MEM_SPACE_EL1N || *num_bytes == 0) {
    *num_bytes = 0;
    return OCSD_OK;
  }

  auto range = AddressRange::FromStartAndSize(address, *num_bytes);

  if (!cached_mapping_ || !cached_mapping_->Contains(range)) {
    PERFETTO_CHECK(tid_);
    cached_mapping_ = memory_->FindMapping(ts_, *tid_, range);
  }

  if (!cached_mapping_) {
    *num_bytes = 0;
    return OCSD_OK;
  }

  *num_bytes = Read(*cached_mapping_, range, dest);
  return OCSD_OK;
}

void TargetMemoryReader::InvalidateMemAccCache(uint8_t) {
  cached_mapping_ = nullptr;
}

void TargetMemoryReader::SetPeContext(const ocsd_pe_context& cxt) {
  PERFETTO_CHECK(cxt.ctxt_id_valid);
  InvalidateMemAccCache(0);
  tid_ = cxt.context_id;
}

const MappingVersion* TargetMemoryReader::FindMapping(uint64_t address) const {
  if (cached_mapping_ && cached_mapping_->Contains(address)) {
    return cached_mapping_;
  }
  PERFETTO_CHECK(tid_);

  return memory_->FindMapping(ts_, *tid_, address);
}

void TargetMemoryReader::SetTs(int64_t ts) {
  ts_ = ts;
  InvalidateMemAccCache(0);
}

}  // namespace perfetto::trace_processor::etm
