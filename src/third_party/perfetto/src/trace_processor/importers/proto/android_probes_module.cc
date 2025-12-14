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

#include <cstdint>
#include <string>
#include <utility>

#include "perfetto/base/logging.h"
#include "perfetto/ext/base/string_view.h"
#include "perfetto/protozero/field.h"
#include "perfetto/protozero/scattered_heap_buffer.h"
#include "perfetto/trace_processor/ref_counted.h"
#include "perfetto/trace_processor/trace_blob.h"
#include "src/trace_processor/importers/common/clock_tracker.h"
#include "src/trace_processor/importers/common/event_tracker.h"
#include "src/trace_processor/importers/common/parser_types.h"
#include "src/trace_processor/importers/proto/android_probes_parser.h"
#include "src/trace_processor/importers/proto/android_probes_tracker.h"
#include "src/trace_processor/importers/proto/packet_sequence_state_generation.h"
#include "src/trace_processor/importers/proto/proto_importer_module.h"
#include "src/trace_processor/sorter/trace_sorter.h"
#include "src/trace_processor/storage/stats.h"
#include "src/trace_processor/storage/trace_storage.h"

#include "protos/perfetto/common/android_energy_consumer_descriptor.pbzero.h"
#include "protos/perfetto/common/android_log_constants.pbzero.h"
#include "protos/perfetto/config/trace_config.pbzero.h"
#include "protos/perfetto/trace/android/android_log.pbzero.h"
#include "protos/perfetto/trace/android/packages_list.pbzero.h"
#include "protos/perfetto/trace/power/android_energy_estimation_breakdown.pbzero.h"
#include "protos/perfetto/trace/power/android_entity_state_residency.pbzero.h"
#include "protos/perfetto/trace/power/power_rails.pbzero.h"
#include "protos/perfetto/trace/trace_packet.pbzero.h"

namespace perfetto::trace_processor {

using perfetto::protos::pbzero::TracePacket;

AndroidProbesModule::AndroidProbesModule(
    ProtoImporterModuleContext* module_context,
    TraceProcessorContext* context)
    : ProtoImporterModule(module_context),
      tracker_(std::make_unique<AndroidProbesTracker>(context->storage.get())),
      parser_(context, tracker_.get()),
      context_(context) {
  RegisterForField(TracePacket::kBatteryFieldNumber);
  RegisterForField(TracePacket::kPowerRailsFieldNumber);
  RegisterForField(TracePacket::kAndroidEnergyEstimationBreakdownFieldNumber);
  RegisterForField(TracePacket::kEntityStateResidencyFieldNumber);
  RegisterForField(TracePacket::kAndroidLogFieldNumber);
  RegisterForField(TracePacket::kPackagesListFieldNumber);
  RegisterForField(TracePacket::kAndroidGameInterventionListFieldNumber);
  RegisterForField(TracePacket::kInitialDisplayStateFieldNumber);
  RegisterForField(TracePacket::kAndroidSystemPropertyFieldNumber);
  RegisterForField(TracePacket::kBluetoothTraceEventFieldNumber);
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

  // Power rails are similar to ftrace in that they have many events, each with
  // their own timestamp, packed inside a single TracePacket. This means that,
  // similar to ftrace, we need to unpack them and individually sort them.
  // These events are not perf sensitive, so not worth adding a lot of machinery
  // to shepherd these events through the sorting queues in a special way.
  // Therefore, we just forge new packets and sort them as if they came from the
  // underlying trace.
  if (field_id == TracePacket::kPowerRailsFieldNumber) {
    auto power_rails = decoder.power_rails();
    protos::pbzero::PowerRails::Decoder evt(power_rails);

    parser_.ParseRailDescriptor(evt);

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

      auto* power_rails_proto = data_packet->set_power_rails();
      power_rails_proto->set_session_uuid(evt.session_uuid());
      auto* energy = power_rails_proto->add_energy_data();
      energy->set_energy(data.energy());
      energy->set_index(data.index());
      energy->set_timestamp_ms(static_cast<uint64_t>(actual_ts / 1000000));

      auto [vec, size] = data_packet.SerializeAsUniquePtr();
      TraceBlobView tbv(TraceBlob::TakeOwnership(std::move(vec), size));
      module_context_->trace_packet_stream->Push(
          actual_ts, TracePacketData{std::move(tbv), state});
    }
    return ModuleResult::Handled();
  }

  // We treat Android logs similarly to ftrace in that they have many events, so
  // we just mimic the sorting logic to the one from kPowerRailsFieldNumber
  // above.
  if (field_id == TracePacket::kAndroidLogFieldNumber) {
    auto android_log = decoder.android_log();
    protos::pbzero::AndroidLogPacket::Decoder pkt(android_log);
    for (auto it = pkt.events(); it; ++it) {
      protos::pbzero::AndroidLogPacket::LogEvent::Decoder evt(*it);
      int64_t realtime_ts = static_cast<int64_t>(evt.timestamp());
      base::StatusOr<int64_t> trace_ts = context_->clock_tracker->ToTraceTime(
          protos::pbzero::BUILTIN_CLOCK_REALTIME, realtime_ts);
      if (!trace_ts.ok()) {
        static std::atomic<uint32_t> dlog_count(0);
        if (dlog_count++ < 10) {
          PERFETTO_DLOG("%s", trace_ts.status().c_message());
        }
        continue;
      }
      int64_t actual_ts = *trace_ts;

      protozero::HeapBuffered<protos::pbzero::TracePacket> data_packet;
      data_packet->set_timestamp(static_cast<uint64_t>(actual_ts));

      auto* log_pkt = data_packet->set_android_log();
      auto* log_evt = log_pkt->add_events();
      log_evt->set_log_id(
          static_cast<protos::pbzero::AndroidLogId>(evt.log_id()));
      log_evt->set_pid(evt.pid());
      log_evt->set_tid(evt.tid());
      log_evt->set_uid(evt.uid());
      log_evt->set_timestamp(evt.timestamp());
      log_evt->set_tag(evt.tag());
      log_evt->set_prio(
          static_cast<protos::pbzero::AndroidLogPriority>(evt.prio()));
      log_evt->set_message(evt.message());
      for (auto arg_it = evt.args(); arg_it; ++arg_it) {
        protos::pbzero::AndroidLogPacket::LogEvent::Arg::Decoder arg(*arg_it);
        auto* new_arg = log_evt->add_args();
        new_arg->set_name(arg.name());
        if (arg.has_int_value()) {
          new_arg->set_int_value(arg.int_value());
        } else if (arg.has_float_value()) {
          new_arg->set_float_value(arg.float_value());
        } else if (arg.has_string_value()) {
          new_arg->set_string_value(arg.string_value());
        }
      }

      auto [vec, size] = data_packet.SerializeAsUniquePtr();
      TraceBlobView tbv(TraceBlob::TakeOwnership(std::move(vec), size));
      module_context_->trace_packet_stream->Push(
          actual_ts, TracePacketData{std::move(tbv), state});
    }
    if (pkt.has_stats()) {
      parser_.ParseAndroidLogStats(pkt.stats());
    }
    return ModuleResult::Handled();
  }

  // Events with a timestamp are pushed to the sorter.
  return ModuleResult::Ignored();
}

void AndroidProbesModule::ParseTracePacketData(
    const TracePacket::Decoder& decoder,
    int64_t ts,
    const TracePacketData&,
    uint32_t field_id) {
  switch (field_id) {
    case TracePacket::kAndroidLogFieldNumber:
      parser_.ParseAndroidLogPacket(ts, decoder.android_log());
      return;
    case TracePacket::kAndroidGameInterventionListFieldNumber:
      parser_.ParseAndroidGameIntervention(
          decoder.android_game_intervention_list());
      return;
    case TracePacket::kPowerRailsFieldNumber:
      parser_.ParsePowerRails(ts, decoder.timestamp(), decoder.power_rails());
      return;
    case TracePacket::kBatteryFieldNumber:
      parser_.ParseBatteryCounters(ts, decoder.battery());
      return;
    case TracePacket::kAndroidEnergyEstimationBreakdownFieldNumber:
      parser_.ParseEnergyBreakdown(
          ts, decoder.android_energy_estimation_breakdown());
      return;
    case TracePacket::kEntityStateResidencyFieldNumber:
      parser_.ParseEntityStateResidency(ts, decoder.entity_state_residency());
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
    default:
      PERFETTO_FATAL("Unexpected field in AndroidProbesModule");
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

    tracker_->SetEnergyBreakdownDescriptor(
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

  for (auto it = pkg_list.packages(); it; ++it) {
    protos::pbzero::PackagesList_PackageInfo::Decoder pkg(*it);
    std::string pkg_name = pkg.name().ToStdString();
    if (!tracker_->ShouldInsertPackage(pkg_name)) {
      continue;
    }
    context_->storage->mutable_package_list_table()->Insert(
        {context_->storage->InternString(pkg.name()),
         static_cast<int64_t>(pkg.uid()), pkg.debuggable(),
         pkg.profileable_from_shell(), pkg.version_code()});
    tracker_->InsertedPackage(std::move(pkg_name));
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
    tracker_->SetEntityStateDescriptor(
        entity_state.entity_index(), entity_state.state_index(),
        context_->storage->InternString(entity_state.entity_name()),
        context_->storage->InternString(entity_state.state_name()));
  }
}

}  // namespace perfetto::trace_processor
