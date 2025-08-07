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

#include "src/trace_processor/importers/common/cpu_tracker.h"

#include <cstdint>
#include <optional>

#include "perfetto/base/logging.h"
#include "perfetto/ext/base/string_view.h"
#include "src/trace_processor/importers/common/machine_tracker.h"
#include "src/trace_processor/tables/metadata_tables_py.h"

namespace perfetto::trace_processor {

CpuTracker::CpuTracker(TraceProcessorContext* context) : context_(context) {
  // Preallocate ucpu of this machine for maintaining the relative order between
  // ucpu and cpu.
  auto machine_id = context_->machine_tracker->machine_id();
  if (machine_id.has_value())
    ucpu_offset_ = machine_id->value * kMaxCpusPerMachine;

  for (auto id = 0u; id < kMaxCpusPerMachine; id++) {
    // Only populate the |machine_id| column. The |cpu| column is update only
    // when the CPU is present.
    tables::CpuTable::Row cpu_row;
    cpu_row.machine_id = machine_id;
    context_->storage->mutable_cpu_table()->Insert(cpu_row);
  }
}

tables::CpuTable::Id CpuTracker::SetCpuInfo(uint32_t cpu,
                                            base::StringView processor,
                                            uint32_t cluster_id,
                                            std::optional<uint32_t> capacity) {
  auto cpu_id = GetOrCreateCpu(cpu);

  auto cpu_row = context_->storage->mutable_cpu_table()->FindById(cpu_id);
  PERFETTO_DCHECK(cpu_row.has_value());

  if (!processor.empty()) {
    auto string_id = context_->storage->InternString(processor);
    cpu_row->set_processor(string_id);
  }
  cpu_row->set_cluster_id(cluster_id);
  if (capacity) {
    cpu_row->set_capacity(*capacity);
  }
  return cpu_id;
}

}  // namespace perfetto::trace_processor
