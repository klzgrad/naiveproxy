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
#include "src/trace_processor/storage/trace_storage.h"

namespace perfetto::trace_processor {

using tables::MachineTable;

MachineTracker::MachineTracker(TraceProcessorContext* context,
                               uint32_t raw_machine_id)
    : context_(context) {
  auto id =
      context_->storage->mutable_machine_table()->Insert({raw_machine_id}).id;

  if (raw_machine_id)
    machine_id_ = id;
}
MachineTracker::~MachineTracker() = default;

void MachineTracker::SetMachineInfo(StringId sysname,
                                    StringId release,
                                    StringId version,
                                    StringId arch) {
  auto row = getRow();

  row->set_sysname(sysname);
  row->set_release(release);
  row->set_version(version);
  row->set_arch(arch);
}

void MachineTracker::SetNumCpus(uint32_t cpus) {
  getRow()->set_num_cpus(cpus);
}

void MachineTracker::SetAndroidBuildFingerprint(StringId build_fingerprint) {
  getRow()->set_android_build_fingerprint(build_fingerprint);
}

void MachineTracker::SetAndroidDeviceManufacturer(
    StringId device_manufacturer) {
  getRow()->set_android_device_manufacturer(device_manufacturer);
}

void MachineTracker::SetAndroidSdkVersion(int64_t sdk_version) {
  getRow()->set_android_sdk_version(sdk_version);
}

PERFETTO_ALWAYS_INLINE
std::optional<MachineTable::RowReference> MachineTracker::getRow() {
  auto& machines = *context_->storage->mutable_machine_table();
  // Host machine has ID 0
  auto machine_id = machine_id_ ? *machine_id_ : MachineTable::Id(0);
  return machines.FindById(machine_id);
}

}  // namespace perfetto::trace_processor
