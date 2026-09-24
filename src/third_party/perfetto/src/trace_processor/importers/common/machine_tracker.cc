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

#include "src/trace_processor/importers/common/machine_tracker.h"

#include <cmath>

#include "src/trace_processor/storage/trace_storage.h"
#include "src/trace_processor/types/trace_manifest_state.h"
#include "src/trace_processor/types/trace_processor_context.h"

namespace perfetto::trace_processor {

using tables::MachineTable;

namespace {
// A perfetto_manifest may have already created this machine's row so that clock
// references could resolve to it before any file forked. Reuse that row;
// otherwise create one now.
MachineTable::Id LookupOrCreateMachineRow(TraceProcessorContext* context,
                                          int64_t raw_machine_id) {
  if (auto* manifest = context->trace_manifest_state.get()) {
    if (uint32_t* row = manifest->raw_id_to_table_id.Find(raw_machine_id)) {
      return MachineTable::Id(*row);
    }
  }
  return context->storage->mutable_machine_table()->Insert({raw_machine_id}).id;
}
}  // namespace

MachineTracker::MachineTracker(TraceProcessorContext* context,
                               int64_t raw_machine_id)
    : machine_id_(LookupOrCreateMachineRow(context, raw_machine_id)),
      context_(context) {}
MachineTracker::~MachineTracker() = default;

void MachineTracker::SetMachineInfo(StringId sysname,
                                    StringId release,
                                    StringId version,
                                    StringId arch) {
  auto row = getRow();

  row.set_sysname(sysname);
  row.set_release(release);
  row.set_version(version);
  row.set_arch(arch);
}

void MachineTracker::SetMachineName(StringId name) {
  getRow().set_name(name);
}

void MachineTracker::SetNumCpus(uint32_t cpus) {
  getRow().set_num_cpus(cpus);
}

void MachineTracker::SetAndroidBuildFingerprint(StringId build_fingerprint) {
  getRow().set_android_build_fingerprint(build_fingerprint);
}

void MachineTracker::SetAndroidDeviceManufacturer(
    StringId device_manufacturer) {
  getRow().set_android_device_manufacturer(device_manufacturer);
}

void MachineTracker::SetAndroidSdkVersion(int64_t sdk_version) {
  getRow().set_android_sdk_version(sdk_version);
}

void MachineTracker::SetSystemRamBytes(int64_t system_ram_bytes) {
  auto row = getRow();
  row.set_system_ram_bytes(system_ram_bytes);
  row.set_system_ram_gb(BytesToGB(system_ram_bytes));
}

void MachineTracker::SetRawMachineId(int64_t raw_machine_id) {
  getRow().set_raw_id(raw_machine_id);
}

int64_t MachineTracker::raw_machine_id() const {
  return context_->storage->machine_table()[machine_id_].raw_id();
}

PERFETTO_ALWAYS_INLINE
MachineTable::RowReference MachineTracker::getRow() {
  auto& machines = *context_->storage->mutable_machine_table();
  return machines[machine_id_];
}

// static
int64_t MachineTracker::BytesToGB(int64_t bytes) {
  return static_cast<int64_t>(std::round(static_cast<double>(bytes) / 1e9));
}

}  // namespace perfetto::trace_processor
