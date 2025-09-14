/*
 * Copyright (C) 2019 The Android Open Source Project
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

#include "src/trace_processor/importers/proto/android_probes_module.h"

#include <cinttypes>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

#include "perfetto/base/logging.h"
#include "perfetto/ext/base/string_utils.h"
#include "perfetto/ext/base/string_view.h"
#include "perfetto/protozero/field.h"
#include "perfetto/protozero/scattered_heap_buffer.h"
#include "perfetto/public/compiler.h"
#include "perfetto/trace_processor/ref_counted.h"
#include "perfetto/trace_processor/trace_blob.h"
#include "src/trace_processor/importers/common/args_tracker.h"
#include "src/trace_processor/importers/common/parser_types.h"
#include "src/trace_processor/importers/common/track_tracker.h"
#include "src/trace_processor/importers/common/tracks.h"
#include "src/trace_processor/importers/common/tracks_common.h"
#include "src/trace_processor/importers/proto/android_probes_parser.h"
#include "src/trace_processor/importers/proto/android_probes_tracker.h"
#include "src/trace_processor/importers/proto/packet_sequence_state_generation.h"
#include "src/trace_processor/importers/proto/proto_importer_module.h"
#include "src/trace_processor/sorter/trace_sorter.h"
#include "src/trace_processor/storage/stats.h"
#include "src/trace_processor/storage/trace_storage.h"
#include "src/trace_processor/types/variadic.h"

#include "protos/perfetto/common/android_energy_consumer_descriptor.pbzero.h"
#include "protos/perfetto/config/trace_config.pbzero.h"
#include "protos/perfetto/trace/android/bluetooth_trace.pbzero.h"
#include "protos/perfetto/trace/android/packages_list.pbzero.h"
#include "protos/perfetto/trace/power/android_energy_estimation_breakdown.pbzero.h"
#include "protos/perfetto/trace/power/android_entity_state_residency.pbzero.h"
#include "protos/perfetto/trace/power/power_rails.pbzero.h"
#include "protos/perfetto/trace/trace_packet.pbzero.h"

namespace perfetto::trace_processor {
namespace {

const char* MapToFriendlyPowerRailName(base::StringView raw) {
  if (raw.EndsWith("M_VDD_CPUCL0")) {
    return "cpu.little";
  } else if (raw.EndsWith("M_VDD_CPUCL0_M")) {
    return "cpu.little.mem";
  } else if (raw.EndsWith("M_VDD_CPUCL1")) {
    return "cpu.mid";
  } else if (raw.EndsWith("M_VDD_CPUCL1_M")) {
    return "cpu.mid.mem";
  } else if (raw.EndsWith("M_VDD_CPUCL2")) {
    return "cpu.big";
  } else if (raw.EndsWith("M_VDD_INT")) {
    return "system.fabric";
  } else if (raw.EndsWith("M_VDD_TPU")) {
    return "tpu";
  } else if (raw.EndsWith("VSYS_PWR_DISP") ||
             raw.EndsWith("VSYS_PWR_DISPLAY")) {
    return "display";
  } else if (raw.EndsWith("M_DISP")) {
    return "ldo.main.a.display";
  } else if (raw.EndsWith("VSYS_PWR_MODEM")) {
    return "modem";
  } else if (raw.EndsWith("M_VDD_MIF")) {
    return "memory.interface";
  } else if (raw.EndsWith("VSYS_PWR_WLAN_BT")) {
    return "wifi.bt";
  } else if (raw.EndsWith("VSYS_PWR_MMWAVE")) {
    return "mmwave";
  } else if (raw.EndsWith("S_VDD_AOC_RET")) {
    return "aoc.memory";
  } else if (raw.EndsWith("S_VDD_AOC")) {
    return "aoc.logic";
  } else if (raw.EndsWith("S_VDDQ_MEM")) {
    return "ddr.a";
  } else if (raw.EndsWith("S_VDD2L") || raw.EndsWith("S_VDD2L_MEM")) {
    return "ddr.b";
  } else if (raw.EndsWith("S_VDD2H_MEM")) {
    return "ddr.c";
  } else if (raw.EndsWith("S_VDD_G3D")) {
    return "gpu";
  } else if (raw.EndsWith("S_VDD_G3D_L2")) {
    return "gpu.l2";
  } else if (raw.EndsWith("S_GNSS_CORE")) {
    return "gps";
  } else if (raw.EndsWith("VSYS_PWR_RFFE")) {
    return "radio.frontend";
  } else if (raw.EndsWith("VSYS_PWR_CAMERA")) {
    return "camera";
  } else if (raw.EndsWith("S_VDD_CAM")) {
    return "multimedia";
  } else if (raw.EndsWith("S_UDFPS")) {
    return "udfps";
  } else if (raw.EndsWith("S_PLL_MIPI_UFS")) {
    return "ufs";
  } else if (raw.EndsWith("M_LLDO1")) {
    return "ldo.main.a";
  } else if (raw.EndsWith("M_LLDO2")) {
    return "ldo.main.b";
  } else if (raw.EndsWith("S_LLDO1")) {
    return "ldo.sub";
  }
  return nullptr;
}

}  // namespace

using perfetto::protos::pbzero::TracePacket;

AndroidProbesModule::AndroidProbesModule(TraceProcessorContext* context)
    : parser_(context),
      context_(context),
      power_rail_raw_name_id_(context->storage->InternString("raw_name")),
      power_rail_subsys_name_arg_id_(
          context->storage->InternString("subsystem_name")) {
  RegisterForField(TracePacket::kBatteryFieldNumber, context);
  RegisterForField(TracePacket::kPowerRailsFieldNumber, context);
  RegisterForField(TracePacket::kAndroidEnergyEstimationBreakdownFieldNumber,
                   context);
  RegisterForField(TracePacket::kEntityStateResidencyFieldNumber, context);
  RegisterForField(TracePacket::kAndroidLogFieldNumber, context);
  RegisterForField(TracePacket::kPackagesListFieldNumber, context);
  RegisterForField(TracePacket::kAndroidGameInterventionListFieldNumber,
                   context);
  RegisterForField(TracePacket::kInitialDisplayStateFieldNumber, context);
  RegisterForField(TracePacket::kAndroidSystemPropertyFieldNumber, context);
  RegisterForField(TracePacket::kBluetoothTraceEventFieldNumber, context);
}

ModuleResult AndroidProbesModule::TokenizePacket(
    const protos::pbzero::TracePacket_Decoder&,
    TraceBlobView* packet,
    int64_t packet_timestamp,
    RefPtr<PacketSequenceStateGeneration> state,
    uint32_t field_id) {
  protos::pbzero::TracePacket::Decoder decoder(packet->data(),
                                               packet->length());

  // The energy descriptor and packages list packets do not have a timestamp so
  // need to be handled at the tokenization phase.
  if (field_id == TracePacket::kAndroidEnergyEstimationBreakdownFieldNumber) {
    return ParseEnergyDescriptor(decoder.android_energy_estimation_breakdown());
  }
  if (field_id == TracePacket::kPackagesListFieldNumber) {
    return ParseAndroidPackagesList(decoder.packages_list());
  }
  if (field_id == TracePacket::kEntityStateResidencyFieldNumber) {
    ParseEntityStateDescriptor(decoder.entity_state_residency());
    // Ignore so that we get a go at parsing any actual residency data that
    // should also be in the packet.
    return ModuleResult::Ignored();
  }

  if (field_id != TracePacket::kPowerRailsFieldNumber) {
    return ModuleResult::Ignored();
  }

  // Power rails are similar to ftrace in that they have many events, each with
  // their own timestamp, packed inside a single TracePacket. This means that,
  // similar to ftrace, we need to unpack them and individually sort them.

  // However, as these events are not perf sensitive, it's not worth adding
  // a lot of machinery to shepherd these events through the sorting queues
  // in a special way. Therefore, we just forge new packets and sort them as if
  // they came from the underlying trace.
  auto power_rails = decoder.power_rails();
  protos::pbzero::PowerRails::Decoder evt(power_rails);

  for (auto it = evt.rail_descriptor(); it; ++it) {
    protos::pbzero::PowerRails::RailDescriptor::Decoder desc(*it);
    uint32_t idx = desc.index();
    if (PERFETTO_UNLIKELY(idx > 256)) {
      PERFETTO_DLOG("Skipping excessively large power_rail index %" PRIu32,
                    idx);
      continue;
    }
    static constexpr auto kPowerBlueprint = tracks::CounterBlueprint(
        "power_rails", tracks::UnknownUnitBlueprint(),
        tracks::DimensionBlueprints(tracks::kNameFromTraceDimensionBlueprint),
        tracks::DynamicNameBlueprint());
    const char* friendly_name = MapToFriendlyPowerRailName(desc.rail_name());
    TrackId track;
    auto args_fn = [this, &desc](ArgsTracker::BoundInserter& inserter) {
      StringId raw_name = context_->storage->InternString(desc.rail_name());
      inserter.AddArg(power_rail_raw_name_id_, Variadic::String(raw_name));

      StringId subsys_name =
          context_->storage->InternString(desc.subsys_name());
      inserter.AddArg(power_rail_subsys_name_arg_id_,
                      Variadic::String(subsys_name));
    };
    if (friendly_name) {
      StringId id = context_->storage->InternString(
          base::StackString<255>("power.rails.%s", friendly_name)
              .string_view());
      track = context_->track_tracker->InternTrack(
          kPowerBlueprint, tracks::Dimensions(desc.rail_name()),
          tracks::DynamicName(id), args_fn);
    } else {
      StringId id = context_->storage->InternString(
          base::StackString<255>("power.%.*s_uws", int(desc.rail_name().size),
                                 desc.rail_name().data)
              .string_view());
      track = context_->track_tracker->InternTrack(
          kPowerBlueprint, tracks::Dimensions(desc.rail_name()),
          tracks::DynamicName(id), args_fn);
    }
    AndroidProbesTracker::GetOrCreate(context_)->SetPowerRailTrack(desc.index(),
                                                                   track);
  }

  // For each energy data message, turn it into its own trace packet
  // making sure its timestamp is consistent between the packet level and
  // the EnergyData level.
  for (auto it = evt.energy_data(); it; ++it) {
    protos::pbzero::PowerRails::EnergyData::Decoder data(*it);
    int64_t actual_ts =
        data.has_timestamp_ms()
            ? static_cast<int64_t>(data.timestamp_ms()) * 1000000
            : packet_timestamp;

    protozero::HeapBuffered<protos::pbzero::TracePacket> data_packet;
    // Keep the original timestamp to later extract as an arg; the sorter does
    // not read this.
    data_packet->set_timestamp(static_cast<uint64_t>(packet_timestamp));

    auto* energy = data_packet->set_power_rails()->add_energy_data();
    energy->set_energy(data.energy());
    energy->set_index(data.index());
    energy->set_timestamp_ms(static_cast<uint64_t>(actual_ts / 1000000));

    std::vector<uint8_t> vec = data_packet.SerializeAsArray();
    TraceBlob blob = TraceBlob::CopyFrom(vec.data(), vec.size());
    context_->sorter->PushTracePacket(actual_ts, state,
                                      TraceBlobView(std::move(blob)),
                                      context_->machine_id());
  }
  return ModuleResult::Handled();
}

void AndroidProbesModule::ParseTracePacketData(
    const TracePacket::Decoder& decoder,
    int64_t ts,
    const TracePacketData&,
    uint32_t field_id) {
  switch (field_id) {
    case TracePacket::kBatteryFieldNumber:
      parser_.ParseBatteryCounters(ts, decoder.battery());
      return;
    case TracePacket::kPowerRailsFieldNumber:
      parser_.ParsePowerRails(ts, decoder.timestamp(), decoder.power_rails());
      return;
    case TracePacket::kAndroidEnergyEstimationBreakdownFieldNumber:
      parser_.ParseEnergyBreakdown(
          ts, decoder.android_energy_estimation_breakdown());
      return;
    case TracePacket::kEntityStateResidencyFieldNumber:
      parser_.ParseEntityStateResidency(ts, decoder.entity_state_residency());
      return;
    case TracePacket::kAndroidLogFieldNumber:
      parser_.ParseAndroidLogPacket(decoder.android_log());
      return;
    case TracePacket::kAndroidGameInterventionListFieldNumber:
      parser_.ParseAndroidGameIntervention(
          decoder.android_game_intervention_list());
      return;
    case TracePacket::kInitialDisplayStateFieldNumber:
      parser_.ParseInitialDisplayState(ts, decoder.initial_display_state());
      return;
    case TracePacket::kAndroidSystemPropertyFieldNumber:
      parser_.ParseAndroidSystemProperty(ts, decoder.android_system_property());
      return;
    case TracePacket::kBluetoothTraceEventFieldNumber:
      parser_.ParseBtTraceEvent(ts, decoder.bluetooth_trace_event());
      return;
  }
}

void AndroidProbesModule::ParseTraceConfig(
    const protos::pbzero::TraceConfig::Decoder& decoder) {
  if (decoder.has_statsd_metadata()) {
    parser_.ParseStatsdMetadata(decoder.statsd_metadata());
  }
}

ModuleResult AndroidProbesModule::ParseEnergyDescriptor(
    protozero::ConstBytes blob) {
  protos::pbzero::AndroidEnergyEstimationBreakdown::Decoder event(blob);
  if (!event.has_energy_consumer_descriptor())
    return ModuleResult::Ignored();

  protos::pbzero::AndroidEnergyConsumerDescriptor::Decoder descriptor(
      event.energy_consumer_descriptor());

  for (auto it = descriptor.energy_consumers(); it; ++it) {
    protos::pbzero::AndroidEnergyConsumer::Decoder consumer(*it);

    if (!consumer.has_energy_consumer_id()) {
      context_->storage->IncrementStats(stats::energy_descriptor_invalid);
      continue;
    }

    AndroidProbesTracker::GetOrCreate(context_)->SetEnergyBreakdownDescriptor(
        consumer.energy_consumer_id(),
        context_->storage->InternString(consumer.name()),
        context_->storage->InternString(consumer.type()), consumer.ordinal());
  }
  return ModuleResult::Handled();
}

ModuleResult AndroidProbesModule::ParseAndroidPackagesList(
    protozero::ConstBytes blob) {
  protos::pbzero::PackagesList::Decoder pkg_list(blob.data, blob.size);
  context_->storage->SetStats(stats::packages_list_has_read_errors,
                              pkg_list.read_error());
  context_->storage->SetStats(stats::packages_list_has_parse_errors,
                              pkg_list.parse_error());

  AndroidProbesTracker* tracker = AndroidProbesTracker::GetOrCreate(context_);
  for (auto it = pkg_list.packages(); it; ++it) {
    protos::pbzero::PackagesList_PackageInfo::Decoder pkg(*it);
    std::string pkg_name = pkg.name().ToStdString();
    if (!tracker->ShouldInsertPackage(pkg_name)) {
      continue;
    }
    context_->storage->mutable_package_list_table()->Insert(
        {context_->storage->InternString(pkg.name()),
         static_cast<int64_t>(pkg.uid()), pkg.debuggable(),
         pkg.profileable_from_shell(),
         static_cast<int64_t>(pkg.version_code())});
    tracker->InsertedPackage(std::move(pkg_name));
  }
  return ModuleResult::Handled();
}

void AndroidProbesModule::ParseEntityStateDescriptor(
    protozero::ConstBytes blob) {
  protos::pbzero::EntityStateResidency::Decoder event(blob);
  if (!event.has_power_entity_state())
    return;

  for (auto it = event.power_entity_state(); it; ++it) {
    protos::pbzero::EntityStateResidency::PowerEntityState::Decoder
        entity_state(*it);

    if (!entity_state.has_entity_index() || !entity_state.has_state_index()) {
      context_->storage->IncrementStats(stats::energy_descriptor_invalid);
      continue;
    }
    AndroidProbesTracker::GetOrCreate(context_)->SetEntityStateDescriptor(
        entity_state.entity_index(), entity_state.state_index(),
        context_->storage->InternString(entity_state.entity_name()),
        context_->storage->InternString(entity_state.state_name()));
  }
}

}  // namespace perfetto::trace_processor
