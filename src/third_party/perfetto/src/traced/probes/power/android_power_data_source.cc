/*
 * Copyright (C) 2018 The Android Open Source Project
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

#include "src/traced/probes/power/android_power_data_source.h"

#include <optional>
#include <vector>

#include "perfetto/base/logging.h"
#include "perfetto/base/task_runner.h"
#include "perfetto/base/time.h"
#include "perfetto/ext/base/scoped_file.h"
#include "perfetto/ext/tracing/core/trace_packet.h"
#include "perfetto/ext/tracing/core/trace_writer.h"
#include "perfetto/tracing/core/data_source_config.h"
#include "src/android_internal/health_hal.h"
#include "src/android_internal/lazy_library_loader.h"
#include "src/android_internal/power_stats.h"

#include "protos/perfetto/common/android_energy_consumer_descriptor.pbzero.h"
#include "protos/perfetto/config/power/android_power_config.pbzero.h"
#include "protos/perfetto/trace/power/android_energy_estimation_breakdown.pbzero.h"
#include "protos/perfetto/trace/power/android_entity_state_residency.pbzero.h"
#include "protos/perfetto/trace/power/battery_counters.pbzero.h"
#include "protos/perfetto/trace/power/power_rails.pbzero.h"
#include "protos/perfetto/trace/trace_packet.pbzero.h"

namespace perfetto {

namespace {
constexpr uint32_t kMinPollIntervalMs = 100;
constexpr uint32_t kDefaultPollIntervalMs = 1000;
constexpr size_t kMaxNumRails = 32;
constexpr size_t kMaxNumEnergyConsumer = 32;
constexpr size_t kMaxNumPowerEntities = 1024;
}  // namespace

// static
const ProbesDataSource::Descriptor AndroidPowerDataSource::descriptor = {
    /*name*/ "android.power",
    /*flags*/ Descriptor::kHandlesIncrementalState,
    /*fill_descriptor_func*/ nullptr,
};

// Dynamically loads the libperfetto_android_internal.so library which
// allows to proxy calls to android hwbinder in in-tree builds.
struct AndroidPowerDataSource::DynamicLibLoader {
  PERFETTO_LAZY_LOAD(android_internal::GetBatteryCounter, get_battery_counter_);
  PERFETTO_LAZY_LOAD(android_internal::GetAvailableRails, get_available_rails_);
  PERFETTO_LAZY_LOAD(android_internal::GetRailEnergyData,
                     get_rail_energy_data_);
  PERFETTO_LAZY_LOAD(android_internal::GetEnergyConsumerInfo,
                     get_energy_consumer_info_);
  PERFETTO_LAZY_LOAD(android_internal::GetEnergyConsumed, get_energy_consumed_);
  PERFETTO_LAZY_LOAD(android_internal::GetPowerEntityStates,
                     get_power_entity_states_);
  PERFETTO_LAZY_LOAD(android_internal::GetPowerEntityStateResidency,
                     get_power_entity_state_residency_);

  std::optional<int64_t> GetCounter(android_internal::BatteryCounter counter) {
    if (!get_battery_counter_)
      return std::nullopt;
    int64_t value = 0;
    if (get_battery_counter_(counter, &value))
      return std::make_optional(value);
    return std::nullopt;
  }

  std::vector<android_internal::RailDescriptor> GetRailDescriptors() {
    if (!get_available_rails_)
      return std::vector<android_internal::RailDescriptor>();

    std::vector<android_internal::RailDescriptor> rail_descriptors(
        kMaxNumRails);
    size_t num_rails = rail_descriptors.size();
    if (!get_available_rails_(&rail_descriptors[0], &num_rails)) {
      PERFETTO_ELOG("Failed to retrieve rail descriptors.");
      num_rails = 0;
    }
    rail_descriptors.resize(num_rails);
    return rail_descriptors;
  }

  std::vector<android_internal::RailEnergyData> GetRailEnergyData() {
    if (!get_rail_energy_data_)
      return std::vector<android_internal::RailEnergyData>();

    std::vector<android_internal::RailEnergyData> energy_data(kMaxNumRails);
    size_t num_rails = energy_data.size();
    if (!get_rail_energy_data_(&energy_data[0], &num_rails)) {
      PERFETTO_ELOG("Failed to retrieve rail energy data.");
      num_rails = 0;
    }
    energy_data.resize(num_rails);
    return energy_data;
  }

  std::vector<android_internal::EnergyConsumerInfo> GetEnergyConsumerInfo() {
    if (!get_energy_consumer_info_)
      return std::vector<android_internal::EnergyConsumerInfo>();

    std::vector<android_internal::EnergyConsumerInfo> consumers(
        kMaxNumEnergyConsumer);
    size_t num_power_entities = consumers.size();
    if (!get_energy_consumer_info_(&consumers[0], &num_power_entities)) {
      PERFETTO_ELOG("Failed to retrieve energy consumer info.");
      num_power_entities = 0;
    }
    consumers.resize(num_power_entities);
    return consumers;
  }

  std::vector<android_internal::EnergyEstimationBreakdown> GetEnergyConsumed() {
    if (!get_energy_consumed_)
      return std::vector<android_internal::EnergyEstimationBreakdown>();

    std::vector<android_internal::EnergyEstimationBreakdown> energy_breakdown(
        kMaxNumPowerEntities);
    size_t num_power_entities = energy_breakdown.size();
    if (!get_energy_consumed_(&energy_breakdown[0], &num_power_entities)) {
      PERFETTO_ELOG("Failed to retrieve energy estimation breakdown.");
      num_power_entities = 0;
    }
    energy_breakdown.resize(num_power_entities);
    return energy_breakdown;
  }

  std::vector<android_internal::PowerEntityState> GetPowerEntityStates() {
    if (!get_power_entity_states_)
      return std::vector<android_internal::PowerEntityState>();

    std::vector<android_internal::PowerEntityState> entity(
        kMaxNumPowerEntities);
    size_t num_power_entities = entity.size();
    if (!get_power_entity_states_(&entity[0], &num_power_entities)) {
      PERFETTO_ELOG("Failed to retrieve power entities.");
      num_power_entities = 0;
    }
    entity.resize(num_power_entities);
    return entity;
  }

  std::vector<android_internal::PowerEntityStateResidency>
  GetPowerEntityStateResidency() {
    if (!get_power_entity_state_residency_)
      return std::vector<android_internal::PowerEntityStateResidency>();

    std::vector<android_internal::PowerEntityStateResidency> entity(
        kMaxNumPowerEntities);
    size_t num_power_entities = entity.size();
    if (!get_power_entity_state_residency_(&entity[0], &num_power_entities)) {
      PERFETTO_ELOG("Failed to retrieve power entities.");
      num_power_entities = 0;
    }
    entity.resize(num_power_entities);
    return entity;
  }
};

AndroidPowerDataSource::AndroidPowerDataSource(
    DataSourceConfig cfg,
    base::TaskRunner* task_runner,
    TracingSessionID session_id,
    std::unique_ptr<TraceWriter> writer)
    : ProbesDataSource(session_id, &descriptor),
      task_runner_(task_runner),
      writer_(std::move(writer)),
      weak_factory_(this) {
  using protos::pbzero::AndroidPowerConfig;
  AndroidPowerConfig::Decoder pcfg(cfg.android_power_config_raw());
  poll_interval_ms_ = pcfg.battery_poll_ms();
  rails_collection_enabled_ = pcfg.collect_power_rails();
  energy_breakdown_collection_enabled_ =
      pcfg.collect_energy_estimation_breakdown();
  entity_state_residency_collection_enabled_ =
      pcfg.collect_entity_state_residency();

  if (poll_interval_ms_ == 0)
    poll_interval_ms_ = kDefaultPollIntervalMs;

  if (poll_interval_ms_ < kMinPollIntervalMs) {
    PERFETTO_ELOG("Battery poll interval of %" PRIu32
                  " ms is too low. Capping to %" PRIu32 " ms",
                  poll_interval_ms_, kMinPollIntervalMs);
    poll_interval_ms_ = kMinPollIntervalMs;
  }
  for (auto counter = pcfg.battery_counters(); counter; ++counter) {
    auto hal_id = android_internal::BatteryCounter::kUnspecified;
    switch (*counter) {
      case AndroidPowerConfig::BATTERY_COUNTER_UNSPECIFIED:
        break;
      case AndroidPowerConfig::BATTERY_COUNTER_CHARGE:
        hal_id = android_internal::BatteryCounter::kCharge;
        break;
      case AndroidPowerConfig::BATTERY_COUNTER_CAPACITY_PERCENT:
        hal_id = android_internal::BatteryCounter::kCapacityPercent;
        break;
      case AndroidPowerConfig::BATTERY_COUNTER_CURRENT:
        hal_id = android_internal::BatteryCounter::kCurrent;
        break;
      case AndroidPowerConfig::BATTERY_COUNTER_CURRENT_AVG:
        hal_id = android_internal::BatteryCounter::kCurrentAvg;
        break;
      case AndroidPowerConfig::BATTERY_COUNTER_VOLTAGE:
        hal_id = android_internal::BatteryCounter::kVoltage;
        break;
    }
    PERFETTO_CHECK(static_cast<size_t>(hal_id) < counters_enabled_.size());
    counters_enabled_.set(static_cast<size_t>(hal_id));
  }
}

AndroidPowerDataSource::~AndroidPowerDataSource() = default;

void AndroidPowerDataSource::Start() {
  lib_.reset(new DynamicLibLoader());
  Tick();
}

void AndroidPowerDataSource::Tick() {
  // Post next task.
  auto now_ms = base::GetWallTimeMs().count();
  auto weak_this = weak_factory_.GetWeakPtr();
  task_runner_->PostDelayedTask(
      [weak_this] {
        if (weak_this)
          weak_this->Tick();
      },
      poll_interval_ms_ - static_cast<uint32_t>(now_ms % poll_interval_ms_));

  if (should_emit_descriptors_) {
    // We write incremental state cleared in its own packet to avoid the subtle
    // code we'd need if we were to set this on the first enabled data source.
    auto packet = writer_->NewTracePacket();
    packet->set_sequence_flags(
        protos::pbzero::TracePacket::SEQ_INCREMENTAL_STATE_CLEARED);
  }

  WriteBatteryCounters();
  WritePowerRailsData();
  WriteEnergyEstimationBreakdown();
  WriteEntityStateResidency();

  should_emit_descriptors_ = false;
}

void AndroidPowerDataSource::WriteBatteryCounters() {
  if (counters_enabled_.none())
    return;

  auto packet = writer_->NewTracePacket();
  packet->set_timestamp(static_cast<uint64_t>(base::GetBootTimeNs().count()));
  auto* counters_proto = packet->set_battery();

  for (size_t i = 0; i < counters_enabled_.size(); i++) {
    if (!counters_enabled_.test(i))
      continue;
    auto counter = static_cast<android_internal::BatteryCounter>(i);
    auto value = lib_->GetCounter(counter);
    if (!value.has_value())
      continue;

    switch (counter) {
      case android_internal::BatteryCounter::kUnspecified:
        PERFETTO_DFATAL("Unspecified counter");
        break;

      case android_internal::BatteryCounter::kCharge:
        counters_proto->set_charge_counter_uah(*value);
        break;

      case android_internal::BatteryCounter::kCapacityPercent:
        counters_proto->set_capacity_percent(static_cast<float>(*value));
        break;

      case android_internal::BatteryCounter::kCurrent:
        counters_proto->set_current_ua(*value);
        break;

      case android_internal::BatteryCounter::kCurrentAvg:
        counters_proto->set_current_avg_ua(*value);
        break;

      case android_internal::BatteryCounter::kVoltage:
        counters_proto->set_voltage_uv(*value);
        break;
    }
  }
}

void AndroidPowerDataSource::WritePowerRailsData() {
  if (!rails_collection_enabled_)
    return;

  auto packet = writer_->NewTracePacket();
  packet->set_timestamp(static_cast<uint64_t>(base::GetBootTimeNs().count()));
  packet->set_sequence_flags(
      protos::pbzero::TracePacket::SEQ_NEEDS_INCREMENTAL_STATE);

  auto* rails_proto = packet->set_power_rails();
  if (should_emit_descriptors_) {
    auto rail_descriptors = lib_->GetRailDescriptors();
    if (rail_descriptors.empty()) {
      // No rails to collect data for. Don't try again.
      rails_collection_enabled_ = false;
      return;
    }

    for (const auto& rail_descriptor : rail_descriptors) {
      auto* rail_desc_proto = rails_proto->add_rail_descriptor();
      rail_desc_proto->set_index(rail_descriptor.index);
      rail_desc_proto->set_rail_name(rail_descriptor.rail_name);
      rail_desc_proto->set_subsys_name(rail_descriptor.subsys_name);
      rail_desc_proto->set_sampling_rate(rail_descriptor.sampling_rate);
    }
  }

  for (const auto& energy_data : lib_->GetRailEnergyData()) {
    auto* data = rails_proto->add_energy_data();
    data->set_index(energy_data.index);
    data->set_timestamp_ms(energy_data.timestamp);
    data->set_energy(energy_data.energy);
  }
}

void AndroidPowerDataSource::WriteEnergyEstimationBreakdown() {
  if (!energy_breakdown_collection_enabled_)
    return;
  auto timestamp = static_cast<uint64_t>(base::GetBootTimeNs().count());

  TraceWriter::TracePacketHandle packet;
  protos::pbzero::AndroidEnergyEstimationBreakdown* energy_estimation_proto =
      nullptr;

  if (should_emit_descriptors_) {
    packet = writer_->NewTracePacket();
    energy_estimation_proto = packet->set_android_energy_estimation_breakdown();
    auto* descriptor_proto =
        energy_estimation_proto->set_energy_consumer_descriptor();
    auto consumers = lib_->GetEnergyConsumerInfo();
    for (const auto& consumer : consumers) {
      auto* desc_proto = descriptor_proto->add_energy_consumers();
      desc_proto->set_energy_consumer_id(consumer.energy_consumer_id);
      desc_proto->set_ordinal(consumer.ordinal);
      desc_proto->set_type(consumer.type);
      desc_proto->set_name(consumer.name);
    }
  }

  auto energy_breakdowns = lib_->GetEnergyConsumed();
  for (const auto& breakdown : energy_breakdowns) {
    if (breakdown.uid == android_internal::ALL_UIDS_FOR_CONSUMER) {
      // Finalize packet before calling NewTracePacket.
      if (packet) {
        packet->Finalize();
      }
      packet = writer_->NewTracePacket();
      packet->set_timestamp(timestamp);
      packet->set_sequence_flags(
          protos::pbzero::TracePacket::SEQ_NEEDS_INCREMENTAL_STATE);

      energy_estimation_proto =
          packet->set_android_energy_estimation_breakdown();
      energy_estimation_proto->set_energy_consumer_id(
          breakdown.energy_consumer_id);
      energy_estimation_proto->set_energy_uws(breakdown.energy_uws);
    } else {
      PERFETTO_CHECK(energy_estimation_proto != nullptr);
      auto* uid_breakdown_proto =
          energy_estimation_proto->add_per_uid_breakdown();
      uid_breakdown_proto->set_uid(breakdown.uid);
      uid_breakdown_proto->set_energy_uws(breakdown.energy_uws);
    }
  }
}

void AndroidPowerDataSource::WriteEntityStateResidency() {
  if (!entity_state_residency_collection_enabled_)
    return;

  auto packet = writer_->NewTracePacket();
  packet->set_timestamp(static_cast<uint64_t>(base::GetBootTimeNs().count()));
  packet->set_sequence_flags(
      protos::pbzero::TracePacket::SEQ_NEEDS_INCREMENTAL_STATE);

  auto* outer_proto = packet->set_entity_state_residency();
  if (should_emit_descriptors_) {
    auto entity_states = lib_->GetPowerEntityStates();
    if (entity_states.empty()) {
      // No entities to collect data for. Don't try again.
      entity_state_residency_collection_enabled_ = false;
      return;
    }

    for (const auto& entity_state : entity_states) {
      auto* entity_state_proto = outer_proto->add_power_entity_state();
      entity_state_proto->set_entity_index(entity_state.entity_id);
      entity_state_proto->set_state_index(entity_state.state_id);
      entity_state_proto->set_entity_name(entity_state.entity_name);
      entity_state_proto->set_state_name(entity_state.state_name);
    }
  }

  for (const auto& residency_data : lib_->GetPowerEntityStateResidency()) {
    auto* data = outer_proto->add_residency();
    data->set_entity_index(residency_data.entity_id);
    data->set_state_index(residency_data.state_id);
    data->set_total_time_in_state_ms(residency_data.total_time_in_state_ms);
    data->set_total_state_entry_count(residency_data.total_state_entry_count);
    data->set_last_entry_timestamp_ms(residency_data.last_entry_timestamp_ms);
  }
}

void AndroidPowerDataSource::Flush(FlushRequestID,
                                   std::function<void()> callback) {
  writer_->Flush(callback);
}

void AndroidPowerDataSource::ClearIncrementalState() {
  should_emit_descriptors_ = true;
}

}  // namespace perfetto
