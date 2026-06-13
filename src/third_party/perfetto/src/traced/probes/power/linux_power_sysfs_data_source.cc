/*
 * Copyright (C) 2021 The Android Open Source Project
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

#include "src/traced/probes/power/linux_power_sysfs_data_source.h"

#include <dirent.h>
#include <sys/types.h>
#include <optional>

#include "perfetto/base/logging.h"
#include "perfetto/base/task_runner.h"
#include "perfetto/base/time.h"
#include "perfetto/ext/base/file_utils.h"
#include "perfetto/ext/base/scoped_file.h"
#include "perfetto/ext/base/string_utils.h"
#include "perfetto/ext/tracing/core/trace_packet.h"
#include "perfetto/ext/tracing/core/trace_writer.h"
#include "perfetto/tracing/core/data_source_config.h"

#include "protos/perfetto/trace/power/battery_counters.pbzero.h"
#include "protos/perfetto/trace/trace_packet.pbzero.h"

namespace perfetto {

namespace {
constexpr uint32_t kDefaultPollIntervalMs = 1000;

std::optional<int64_t> ReadFileAsInt64(std::string path) {
  std::string buf;
  if (!base::ReadFile(path, &buf))
    return std::nullopt;
  return base::StringToInt64(base::StripSuffix(buf, "\n"));
}
}  // namespace

LinuxPowerSysfsDataSource::BatteryInfo::BatteryInfo(
    const char* power_supply_dir_path)
    : power_supply_dir_path_(power_supply_dir_path) {
  base::ScopedDir power_supply_dir(opendir(power_supply_dir_path_.c_str()));
  if (!power_supply_dir)
    return;

  for (auto* ent = readdir(power_supply_dir.get()); ent;
       ent = readdir(power_supply_dir.get())) {
    if (ent->d_name[0] == '.')
      continue;
    std::string dir_name =
        std::string(power_supply_dir_path) + "/" + ent->d_name;
    std::string buf;
    if (!base::ReadFile(dir_name + "/type", &buf) ||
        base::StripSuffix(buf, "\n") != "Battery")
      continue;
    buf.clear();
    if (!base::ReadFile(dir_name + "/present", &buf) ||
        base::StripSuffix(buf, "\n") != "1")
      continue;
    sysfs_battery_subdirs_.push_back(ent->d_name);
  }
}
LinuxPowerSysfsDataSource::BatteryInfo::~BatteryInfo() = default;

size_t LinuxPowerSysfsDataSource::BatteryInfo::num_batteries() const {
  return sysfs_battery_subdirs_.size();
}

std::optional<int64_t>
LinuxPowerSysfsDataSource::BatteryInfo::GetChargeCounterUah(
    size_t battery_idx) {
  PERFETTO_CHECK(battery_idx < sysfs_battery_subdirs_.size());
  return ReadFileAsInt64(power_supply_dir_path_ + "/" +
                         sysfs_battery_subdirs_[battery_idx] + "/charge_now");
}

std::optional<int64_t>
LinuxPowerSysfsDataSource::BatteryInfo::GetEnergyCounterUah(
    size_t battery_idx) {
  PERFETTO_CHECK(battery_idx < sysfs_battery_subdirs_.size());
  return ReadFileAsInt64(power_supply_dir_path_ + "/" +
                         sysfs_battery_subdirs_[battery_idx] + "/energy_now");
}

std::optional<int64_t> LinuxPowerSysfsDataSource::BatteryInfo::GetVoltageUv(
    size_t battery_idx) {
  PERFETTO_CHECK(battery_idx < sysfs_battery_subdirs_.size());
  return ReadFileAsInt64(power_supply_dir_path_ + "/" +
                         sysfs_battery_subdirs_[battery_idx] + "/voltage_now");
}

std::optional<int64_t>
LinuxPowerSysfsDataSource::BatteryInfo::GetCapacityPercent(size_t battery_idx) {
  PERFETTO_CHECK(battery_idx < sysfs_battery_subdirs_.size());
  return ReadFileAsInt64(power_supply_dir_path_ + "/" +
                         sysfs_battery_subdirs_[battery_idx] + "/capacity");
}

std::optional<int64_t> LinuxPowerSysfsDataSource::BatteryInfo::GetCurrentNowUa(
    size_t battery_idx) {
  PERFETTO_CHECK(battery_idx < sysfs_battery_subdirs_.size());
  return ReadFileAsInt64(power_supply_dir_path_ + "/" +
                         sysfs_battery_subdirs_[battery_idx] + "/current_now");
}

std::optional<int64_t>
LinuxPowerSysfsDataSource::BatteryInfo::GetAverageCurrentUa(
    size_t battery_idx) {
  PERFETTO_CHECK(battery_idx < sysfs_battery_subdirs_.size());
  return ReadFileAsInt64(power_supply_dir_path_ + "/" +
                         sysfs_battery_subdirs_[battery_idx] + "/current_avg");
}

std::string LinuxPowerSysfsDataSource::BatteryInfo::GetBatteryName(
    size_t battery_idx) {
  PERFETTO_CHECK(battery_idx < sysfs_battery_subdirs_.size());
  return sysfs_battery_subdirs_[battery_idx];
}

// static
const ProbesDataSource::Descriptor LinuxPowerSysfsDataSource::descriptor = {
    /*name*/ "linux.sysfs_power",
    /*flags*/ Descriptor::kFlagsNone,
    /*fill_descriptor_func*/ nullptr,
};

LinuxPowerSysfsDataSource::LinuxPowerSysfsDataSource(
    DataSourceConfig cfg,
    base::TaskRunner* task_runner,
    TracingSessionID session_id,
    std::unique_ptr<TraceWriter> writer)
    : ProbesDataSource(session_id, &descriptor),
      poll_interval_ms_(kDefaultPollIntervalMs),
      task_runner_(task_runner),
      writer_(std::move(writer)),
      weak_factory_(this) {
  base::ignore_result(cfg);  // The data source doesn't need any config yet.
}

LinuxPowerSysfsDataSource::~LinuxPowerSysfsDataSource() = default;

void LinuxPowerSysfsDataSource::Start() {
  battery_info_.reset(new BatteryInfo());
  Tick();
}

void LinuxPowerSysfsDataSource::Tick() {
  // Post next task.
  auto now_ms = base::GetWallTimeMs().count();
  auto weak_this = weak_factory_.GetWeakPtr();
  task_runner_->PostDelayedTask(
      [weak_this] {
        if (weak_this)
          weak_this->Tick();
      },
      poll_interval_ms_ - static_cast<uint32_t>(now_ms % poll_interval_ms_));

  WriteBatteryCounters();
}

void LinuxPowerSysfsDataSource::WriteBatteryCounters() {
  // Query battery counters from sysfs. Report the battery counters for each
  // battery.
  for (size_t battery_idx = 0; battery_idx < battery_info_->num_batteries();
       battery_idx++) {
    auto packet = writer_->NewTracePacket();
    packet->set_timestamp(static_cast<uint64_t>(base::GetBootTimeNs().count()));

    auto* counters_proto = packet->set_battery();
    auto value = battery_info_->GetChargeCounterUah(battery_idx);
    if (value)
      counters_proto->set_charge_counter_uah(*value);
    value = battery_info_->GetCapacityPercent(battery_idx);
    if (value)
      counters_proto->set_capacity_percent(static_cast<float>(*value));
    value = battery_info_->GetCurrentNowUa(battery_idx);
    if (value)
      counters_proto->set_current_ua(*value);
    value = battery_info_->GetAverageCurrentUa(battery_idx);
    if (value)
      counters_proto->set_current_ua(*value);
    value = battery_info_->GetEnergyCounterUah(battery_idx);
    if (value)
      counters_proto->set_energy_counter_uwh(*value);
    value = battery_info_->GetVoltageUv(battery_idx);
    if (value)
      counters_proto->set_voltage_uv(*value);
    // On systems with multiple batteries, disambiguate with battery names.
    if (battery_info_->num_batteries() > 1)
      counters_proto->set_name(battery_info_->GetBatteryName(battery_idx));
  }
}

void LinuxPowerSysfsDataSource::Flush(FlushRequestID,
                                      std::function<void()> callback) {
  writer_->Flush(callback);
}

}  // namespace perfetto
