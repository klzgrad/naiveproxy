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

#include "src/trace_processor/importers/etm/virtual_address_space.h"

#include <algorithm>
#include <cstdint>
#include <limits>
#include <optional>
#include <set>
#include <utility>
#include <vector>

#include "perfetto/base/logging.h"
#include "perfetto/trace_processor/trace_blob_view.h"
#include "src/trace_processor/importers/common/address_range.h"
#include "src/trace_processor/importers/common/registered_file_tracker.h"
#include "src/trace_processor/importers/etm/mapping_version.h"
#include "src/trace_processor/storage/trace_storage.h"
#include "src/trace_processor/tables/perf_tables_py.h"
#include "src/trace_processor/tables/profiler_tables_py.h"
#include "src/trace_processor/types/trace_processor_context.h"

namespace perfetto::trace_processor::etm {

void VirtualAddressSpace::Builder::AddMapping(
    tables::MmapRecordTable::ConstRowReference mmap) {
  const auto mapping =
      *context_->storage->stack_profile_mapping_table().FindById(
          mmap.mapping_id());
  if (static_cast<uint64_t>(mapping.start()) >=
      static_cast<uint64_t>(mapping.end())) {
    return;
  }

  AddressRange range(static_cast<uint64_t>(mapping.start()),
                     static_cast<uint64_t>(mapping.end()));

  std::optional<TraceBlob> content;
  if (mmap.file_id()) {
    TraceBlob& blob =
        context_->registered_file_tracker->GetContent(*mmap.file_id());
    auto file_range = AddressRange::FromStartAndSize(0, blob.size());
    auto required_file_range = AddressRange::FromStartAndSize(
        static_cast<uint64_t>(mapping.exact_offset()), range.size());

    PERFETTO_CHECK(file_range.Contains(required_file_range));
    // TODO(rasikanavarange): The following copy is not efficient and will need
    // clean up later
    content = TraceBlob::CopyFrom(blob.data() + required_file_range.start(),
                                  required_file_range.length());
  }

  auto [it, success] = mappings_.insert(
      MappingVersion(mmap.mapping_id(), mmap.ts(), range, std::move(content)));
  PERFETTO_CHECK(success);
  vertices_.insert(it->start());
  vertices_.insert(it->end());
}

VirtualAddressSpace VirtualAddressSpace::Builder::Build() && {
  std::vector<MappingVersion> slabs;
  // Go over the mappins and process each vertex.
  while (!mappings_.empty()) {
    auto node = mappings_.extract(mappings_.begin());
    auto end = vertices_.begin();
    // Mapping starts at the vertex, noting to do.
    while (*end <= node.value().start()) {
      end = vertices_.erase(end);
    }
    // The mapping ends at this vertex, no need to split it.
    if (node.value().end() == *end) {
      slabs.push_back(std::move(node.value()));
    }
    // Split needed
    else {
      slabs.push_back(node.value().SplitFront(*end));
      mappings_.insert(std::move(node));
    }
  }

  return VirtualAddressSpace(std::move(slabs));
}

const MappingVersion* VirtualAddressSpace::FindMapping(int64_t ts,
                                                       uint64_t address) const {
  if (address == std::numeric_limits<uint64_t>::max()) {
    return nullptr;
  }

  auto it = std::lower_bound(mappings_.begin(), mappings_.end(),
                             LookupKey{address, ts}, Lookup());

  if (it == mappings_.end() || address < it->start()) {
    return nullptr;
  }
  return &*it;
}

}  // namespace perfetto::trace_processor::etm
