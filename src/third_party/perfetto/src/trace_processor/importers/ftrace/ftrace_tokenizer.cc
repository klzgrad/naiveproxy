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

#include "src/trace_processor/importers/ftrace/ftrace_tokenizer.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <utility>
#include <vector>

#include "perfetto/base/logging.h"
#include "perfetto/base/status.h"
#include "perfetto/ext/base/status_macros.h"
#include "perfetto/ext/base/status_or.h"
#include "perfetto/protozero/field.h"
#include "perfetto/protozero/proto_decoder.h"
#include "perfetto/protozero/proto_utils.h"
#include "perfetto/public/compiler.h"
#include "perfetto/trace_processor/basic_types.h"
#include "perfetto/trace_processor/ref_counted.h"
#include "perfetto/trace_processor/trace_blob_view.h"
#include "src/trace_processor/importers/common/clock_tracker.h"
#include "src/trace_processor/importers/common/metadata_tracker.h"
#include "src/trace_processor/importers/common/parser_types.h"
#include "src/trace_processor/importers/ftrace/generic_ftrace_tracker.h"
#include "src/trace_processor/importers/proto/packet_sequence_state_generation.h"
#include "src/trace_processor/importers/proto/proto_importer_module.h"
#include "src/trace_processor/sorter/trace_sorter.h"  // IWYU pragma: keep
#include "src/trace_processor/storage/metadata.h"
#include "src/trace_processor/storage/stats.h"
#include "src/trace_processor/storage/trace_storage.h"
#include "src/trace_processor/types/trace_processor_context.h"
#include "src/trace_processor/types/variadic.h"

#include "protos/perfetto/common/builtin_clock.pbzero.h"
#include "protos/perfetto/trace/ftrace/cpm_trace.pbzero.h"
#include "protos/perfetto/trace/ftrace/ftrace_event.pbzero.h"
#include "protos/perfetto/trace/ftrace/ftrace_event_bundle.pbzero.h"
#include "protos/perfetto/trace/ftrace/fwtp_ftrace.pbzero.h"
#include "protos/perfetto/trace/ftrace/power.pbzero.h"
#include "protos/perfetto/trace/ftrace/thermal_exynos.pbzero.h"
#include "src/trace_processor/util/clock_synchronizer.h"

namespace perfetto::trace_processor {

using protozero::ProtoDecoder;
using protozero::proto_utils::MakeTagVarInt;
using protozero::proto_utils::ParseVarInt;

using protos::pbzero::BuiltinClock;
using protos::pbzero::FtraceClock;
using protos::pbzero::FtraceEventBundle;

namespace {

constexpr uint32_t kSequenceScopedClockId = 64;

// Fast path for parsing the event id of an ftrace event.
// Speculate on the fact that, if the timestamp was found, the common pid
// will appear immediately after and the event id immediately after that.
uint64_t TryFastParseFtraceEventId(const uint8_t* start, const uint8_t* end) {
  constexpr auto kPidFieldNumber = protos::pbzero::FtraceEvent::kPidFieldNumber;
  constexpr auto kPidFieldTag = MakeTagVarInt(kPidFieldNumber);

  // If the next byte is not the common pid's tag, just skip the field.
  constexpr uint32_t kMaxPidLength = 5;
  if (PERFETTO_UNLIKELY(static_cast<uint32_t>(end - start) <= kMaxPidLength ||
                        start[0] != kPidFieldTag)) {
    return 0;
  }

  // Skip the common pid.
  uint64_t common_pid = 0;
  const uint8_t* common_pid_end = ParseVarInt(start + 1, end, &common_pid);
  if (PERFETTO_UNLIKELY(common_pid_end == start + 1)) {
    return 0;
  }

  // Read the next varint: this should be the event id tag.
  uint64_t event_tag = 0;
  const uint8_t* event_id_end = ParseVarInt(common_pid_end, end, &event_tag);
  if (event_id_end == common_pid_end) {
    return 0;
  }

  // The event wire type should be length delimited.
  auto wire_type = static_cast<protozero::proto_utils::ProtoWireType>(
      protozero::proto_utils::GetTagFieldType(event_tag));
  if (wire_type != protozero::proto_utils::ProtoWireType::kLengthDelimited) {
    return 0;
  }
  return protozero::proto_utils::GetTagFieldId(event_tag);
}

}  // namespace

PERFETTO_ALWAYS_INLINE
base::Status FtraceTokenizer::TokenizeFtraceBundle(
    TraceBlobView bundle,
    RefPtr<PacketSequenceStateGeneration> state,
    uint32_t packet_sequence_id) {
  protos::pbzero::FtraceEventBundle::Decoder decoder(bundle.data(),
                                                     bundle.length());

  if (PERFETTO_UNLIKELY(!decoder.has_cpu())) {
    PERFETTO_ELOG("CPU field not found in FtraceEventBundle");
    context_->storage->IncrementStats(stats::ftrace_bundle_tokenizer_errors);
    return base::OkStatus();
  }

  uint32_t cpu = decoder.cpu();
  static constexpr uint32_t kMaxCpuCount = 1024;
  if (PERFETTO_UNLIKELY(cpu >= kMaxCpuCount)) {
    return base::ErrStatus(
        "CPU %u is greater than maximum allowed of %u. This is likely because "
        "of trace corruption",
        cpu, kMaxCpuCount);
  }

  if (PERFETTO_UNLIKELY(decoder.lost_events())) {
    // If set, it means that the kernel overwrote an unspecified number of
    // events since our last read from the per-cpu buffer.
    context_->storage->SetIndexedStats(stats::ftrace_cpu_has_data_loss,
                                       static_cast<int>(cpu), 1);
  }

  // Deal with ftrace recorded using a clock that isn't our preferred default
  // (boottime). Do a best-effort fit to the "primary trace clock" based on
  // per-bundle timestamp snapshots.
  ClockTracker::ClockId clock_id =
      ClockId::Machine(BuiltinClock::BUILTIN_CLOCK_BOOTTIME);
  if (decoder.has_ftrace_clock()) {
    ASSIGN_OR_RETURN(clock_id,
                     HandleFtraceClockSnapshot(decoder, packet_sequence_id));
  }

  if (decoder.has_compact_sched()) {
    TokenizeFtraceCompactSched(cpu, clock_id, decoder.compact_sched());
  }

  for (auto it = decoder.event(); it; ++it) {
    TokenizeFtraceEvent(cpu, clock_id, bundle.slice(it->data(), it->size()),
                        state);
  }

  // v50+: optional proto descriptors for generic (i.e. not known at
  // compile-time) ftrace events.
  for (auto it = decoder.generic_event_descriptors(); it; ++it) {
    protos::pbzero::FtraceEventBundle::GenericEventDescriptor::Decoder
        gen_decoder(it->data(), it->size());
    generic_tracker_->AddDescriptor(
        static_cast<uint32_t>(gen_decoder.field_id()),
        gen_decoder.event_descriptor());
  }

  // First bundle on each cpu is special since ftrace is recorded in per-cpu
  // buffers. In traces written by perfetto v44+ we know the timestamp from
  // which this cpu's data stream is valid. This is important for parsing ring
  // buffer traces, as not all per-cpu data streams will be valid from the same
  // timestamp.
  if (cpu >= per_cpu_seen_first_bundle_.size()) {
    per_cpu_seen_first_bundle_.resize(cpu + 1);
  }
  if (!per_cpu_seen_first_bundle_[cpu]) {
    per_cpu_seen_first_bundle_[cpu] = true;

    // If this cpu's timestamp is the new max, update the metadata table entry.
    // previous_bundle_end_timestamp is the replacement for
    // last_read_event_timestamp on perfetto v47+, at most one will be set.
    if (decoder.has_previous_bundle_end_timestamp() ||
        decoder.has_last_read_event_timestamp()) {
      uint64_t raw_ts = decoder.has_previous_bundle_end_timestamp()
                            ? decoder.previous_bundle_end_timestamp()
                            : decoder.last_read_event_timestamp();
      std::optional<int64_t> timestamp_opt =
          context_->clock_tracker->ToTraceTime(clock_id,
                                               static_cast<int64_t>(raw_ts));
      if (!timestamp_opt.has_value()) {
        return base::ErrStatus("Failed to convert timestamp to trace time");
      }
      int64_t timestamp = *timestamp_opt;

      std::optional<SqlValue> curr_latest_timestamp =
          context_->metadata_tracker->GetMetadata(
              metadata::ftrace_latest_data_start_ns);

      if (!curr_latest_timestamp.has_value() ||
          timestamp > curr_latest_timestamp->AsLong()) {
        context_->metadata_tracker->SetMetadata(
            metadata::ftrace_latest_data_start_ns,
            Variadic::Integer(timestamp));
      }
    }
  }
  return base::OkStatus();
}

PERFETTO_ALWAYS_INLINE
void FtraceTokenizer::TokenizeFtraceEvent(
    uint32_t cpu,
    ClockTracker::ClockId clock_id,
    TraceBlobView event,
    RefPtr<PacketSequenceStateGeneration> state) {
  constexpr auto kTimestampFieldNumber =
      protos::pbzero::FtraceEvent::kTimestampFieldNumber;
  constexpr auto kTimestampFieldTag = MakeTagVarInt(kTimestampFieldNumber);

  const uint8_t* data = event.data();
  const size_t length = event.length();

  // Speculate on the following sequence of varints
  //  - timestamp tag
  //  - timestamp (64 bit)
  //  - common pid tag
  //  - common pid (32 bit)
  //  - event tag
  uint64_t raw_timestamp = 0;
  bool timestamp_found = false;
  uint64_t event_id = 0;
  if (PERFETTO_LIKELY(length > 10 && data[0] == kTimestampFieldTag)) {
    // Fastpath.
    const uint8_t* ts_end = ParseVarInt(data + 1, data + 11, &raw_timestamp);
    timestamp_found = ts_end != data + 1;
    if (PERFETTO_LIKELY(timestamp_found)) {
      event_id = TryFastParseFtraceEventId(ts_end, data + length);
    }
  }

  // Slowpath for finding the timestamp.
  if (PERFETTO_UNLIKELY(!timestamp_found)) {
    ProtoDecoder decoder(data, length);
    if (auto ts_field = decoder.FindField(kTimestampFieldNumber)) {
      timestamp_found = true;
      raw_timestamp = ts_field.as_uint64();
    }
    if (PERFETTO_UNLIKELY(!timestamp_found)) {
      context_->storage->IncrementStats(stats::ftrace_bundle_tokenizer_errors);
      return;
    }
  }

  // Slowpath for finding the event id.
  if (PERFETTO_UNLIKELY(event_id == 0)) {
    ProtoDecoder decoder(data, length);
    for (auto f = decoder.ReadField(); f.valid(); f = decoder.ReadField()) {
      // Find the first length-delimited tag as this corresponds to the ftrace
      // event.
      if (f.type() == protozero::proto_utils::ProtoWireType::kLengthDelimited) {
        event_id = f.id();
        break;
      }
    }
    if (PERFETTO_UNLIKELY(event_id == 0)) {
      context_->storage->IncrementStats(stats::ftrace_missing_event_id);
      return;
    }
  }

  if (PERFETTO_UNLIKELY(
          event_id == protos::pbzero::FtraceEvent::kGpuWorkPeriodFieldNumber)) {
    TokenizeFtraceGpuWorkPeriod(cpu, std::move(event), std::move(state));
    return;
  }
  if (PERFETTO_UNLIKELY(
          event_id ==
          protos::pbzero::FtraceEvent::kThermalExynosAcpmBulkFieldNumber)) {
    TokenizeFtraceThermalExynosAcpmBulk(cpu, std::move(event),
                                        std::move(state));
    return;
  }
  if (PERFETTO_UNLIKELY(
          event_id ==
          protos::pbzero::FtraceEvent::kParamSetValueCpmFieldNumber)) {
    TokenizeFtraceParamSetValueCpm(cpu, std::move(event), std::move(state));
    return;
  }
  if (PERFETTO_UNLIKELY(
          event_id ==
          protos::pbzero::FtraceEvent::kFwtpPerfettoCounterFieldNumber)) {
    TokenizeFtraceFwtpPerfettoCounter(cpu, std::move(event), std::move(state));
    return;
  }
  if (PERFETTO_UNLIKELY(
          event_id ==
          protos::pbzero::FtraceEvent::kFwtpPerfettoSliceFieldNumber)) {
    TokenizeFtraceFwtpPerfettoSlice(cpu, std::move(event), std::move(state));
    return;
  }

  std::optional<int64_t> timestamp = context_->clock_tracker->ToTraceTime(
      clock_id, static_cast<int64_t>(raw_timestamp));
  // ClockTracker will increment some error stats if it failed to convert the
  // timestamp so just return.
  if (!timestamp.has_value()) {
    return;
  }
  module_context_->PushFtraceEvent(
      cpu, *timestamp, TracePacketData{std::move(event), std::move(state)});
}

PERFETTO_ALWAYS_INLINE
void FtraceTokenizer::TokenizeFtraceCompactSched(uint32_t cpu,
                                                 ClockTracker::ClockId clock_id,
                                                 protozero::ConstBytes packet) {
  FtraceEventBundle::CompactSched::Decoder compact_sched(packet);

  // Build the interning table for comm fields.
  std::vector<StringId> string_table;
  string_table.reserve(512);
  for (auto it = compact_sched.intern_table(); it; it++) {
    StringId value = context_->storage->InternString(*it);
    string_table.push_back(value);
  }

  TokenizeFtraceCompactSchedSwitch(cpu, clock_id, compact_sched, string_table);
  TokenizeFtraceCompactSchedWaking(cpu, clock_id, compact_sched, string_table);
}

void FtraceTokenizer::TokenizeFtraceCompactSchedSwitch(
    uint32_t cpu,
    ClockTracker::ClockId clock_id,
    const FtraceEventBundle::CompactSched::Decoder& compact,
    const std::vector<StringId>& string_table) {
  // Accumulator for timestamp deltas.
  int64_t timestamp_acc = 0;

  // The events' fields are stored in a structure-of-arrays style, using packed
  // repeated fields. Walk each repeated field in step to recover individual
  // events.
  bool parse_error = false;
  auto timestamp_it = compact.switch_timestamp(&parse_error);
  auto pstate_it = compact.switch_prev_state(&parse_error);
  auto npid_it = compact.switch_next_pid(&parse_error);
  auto nprio_it = compact.switch_next_prio(&parse_error);
  auto comm_it = compact.switch_next_comm_index(&parse_error);
  for (; timestamp_it && pstate_it && npid_it && nprio_it && comm_it;
       ++timestamp_it, ++pstate_it, ++npid_it, ++nprio_it, ++comm_it) {
    InlineSchedSwitch event{};

    // delta-encoded timestamp
    timestamp_acc += static_cast<int64_t>(*timestamp_it);
    int64_t event_timestamp = timestamp_acc;

    // index into the interned string table
    if (PERFETTO_UNLIKELY(*comm_it >= string_table.size())) {
      parse_error = true;
      break;
    }
    event.next_comm = string_table[*comm_it];

    event.prev_state = *pstate_it;
    event.next_pid = *npid_it;
    event.next_prio = *nprio_it;

    std::optional<int64_t> timestamp =
        context_->clock_tracker->ToTraceTime(clock_id, event_timestamp);
    if (!timestamp.has_value()) {
      return;
    }
    module_context_->PushInlineSchedSwitch(cpu, *timestamp, event);
  }

  // Check that all packed buffers were decoded correctly, and fully.
  bool sizes_match =
      !timestamp_it && !pstate_it && !npid_it && !nprio_it && !comm_it;
  if (parse_error || !sizes_match)
    context_->storage->IncrementStats(stats::compact_sched_has_parse_errors);
}

void FtraceTokenizer::TokenizeFtraceCompactSchedWaking(
    uint32_t cpu,
    ClockTracker::ClockId clock_id,
    const FtraceEventBundle::CompactSched::Decoder& compact,
    const std::vector<StringId>& string_table) {
  // Accumulator for timestamp deltas.
  int64_t timestamp_acc = 0;

  // The events' fields are stored in a structure-of-arrays style, using packed
  // repeated fields. Walk each repeated field in step to recover individual
  // events.
  bool parse_error = false;
  auto timestamp_it = compact.waking_timestamp(&parse_error);
  auto pid_it = compact.waking_pid(&parse_error);
  auto tcpu_it = compact.waking_target_cpu(&parse_error);
  auto prio_it = compact.waking_prio(&parse_error);
  auto comm_it = compact.waking_comm_index(&parse_error);
  auto common_flags_it = compact.waking_common_flags(&parse_error);

  for (; timestamp_it && pid_it && tcpu_it && prio_it && comm_it;
       ++timestamp_it, ++pid_it, ++tcpu_it, ++prio_it, ++comm_it) {
    InlineSchedWaking event{};

    // delta-encoded timestamp
    timestamp_acc += static_cast<int64_t>(*timestamp_it);
    int64_t event_timestamp = timestamp_acc;

    // index into the interned string table
    if (PERFETTO_UNLIKELY(*comm_it >= string_table.size())) {
      parse_error = true;
      break;
    }
    event.comm = string_table[*comm_it];

    event.pid = *pid_it;
    event.target_cpu = static_cast<uint16_t>(*tcpu_it);
    event.prio = static_cast<uint16_t>(*prio_it);

    if (common_flags_it) {
      event.common_flags = static_cast<uint16_t>(*common_flags_it);
      common_flags_it++;
    }

    std::optional<int64_t> timestamp =
        context_->clock_tracker->ToTraceTime(clock_id, event_timestamp);
    if (!timestamp.has_value()) {
      return;
    }
    module_context_->PushInlineSchedWaking(cpu, *timestamp, event);
  }

  // Check that all packed buffers were decoded correctly, and fully.
  bool sizes_match =
      !timestamp_it && !pid_it && !tcpu_it && !prio_it && !comm_it;
  if (parse_error || !sizes_match)
    context_->storage->IncrementStats(stats::compact_sched_has_parse_errors);
}

base::StatusOr<ClockTracker::ClockId>
FtraceTokenizer::HandleFtraceClockSnapshot(
    protos::pbzero::FtraceEventBundle::Decoder& decoder,
    uint32_t packet_sequence_id) {
  // Convert from ftrace clock enum to a clock id for trace parsing.
  ClockTracker::ClockId clock_id =
      ClockId::Machine(BuiltinClock::BUILTIN_CLOCK_BOOTTIME);
  switch (decoder.ftrace_clock()) {
    case FtraceClock::FTRACE_CLOCK_UNSPECIFIED:
      clock_id = ClockId::Machine(BuiltinClock::BUILTIN_CLOCK_BOOTTIME);
      break;
    case FtraceClock::FTRACE_CLOCK_MONO_RAW:
      clock_id = ClockId::Machine(BuiltinClock::BUILTIN_CLOCK_MONOTONIC_RAW);
      break;
    case FtraceClock::FTRACE_CLOCK_GLOBAL:
    case FtraceClock::FTRACE_CLOCK_LOCAL:
      // Per-cpu tracing clocks that aren't normally available in userspace.
      // "local" (aka sched_clock in the kernel) does not guarantee ordering for
      // events happening on different cpus, but is typically coherent enough
      // for us to render the trace. (Overall skew is ~2ms per hour against
      // boottime on a modern arm64 phone.)
      //
      // Treat this as a sequence-scoped clock, using the timestamp pair from
      // cpu0 as recorded in the bundle. Note: the timestamps will be in the
      // future relative to the data covered by the bundle, as the timestamping
      // is done at ftrace read time.
      clock_id = ClockId::Sequence(context_->trace_id().value,
                                   packet_sequence_id, kSequenceScopedClockId);
      break;
    default:
      return base::ErrStatus(
          "Unable to parse ftrace packets with unknown clock");
  }

  // Add the {boottime, clock_id} timestamp pair as a clock snapshot, skipping
  // duplicates since multiple sequential ftrace bundles can share a snapshot.
  if (decoder.has_ftrace_timestamp() && decoder.has_boot_timestamp() &&
      latest_ftrace_clock_snapshot_ts_ != decoder.ftrace_timestamp()) {
    PERFETTO_DCHECK(clock_id !=
                    ClockId::Machine(BuiltinClock::BUILTIN_CLOCK_BOOTTIME));
    int64_t ftrace_timestamp = decoder.ftrace_timestamp();
    auto x = context_->clock_tracker->AddSnapshot({
        ClockTracker::ClockTimestamp(clock_id, ftrace_timestamp),
        ClockTracker::ClockTimestamp(
            ClockId::Machine(BuiltinClock::BUILTIN_CLOCK_BOOTTIME),
            decoder.boot_timestamp()),
    });
    PERFETTO_ELOG("%s", x.ok() ? "Added ftrace clock snapshot"
                               : x.status().message().c_str());
    latest_ftrace_clock_snapshot_ts_ = ftrace_timestamp;
  }
  return clock_id;
}

void FtraceTokenizer::TokenizeFtraceGpuWorkPeriod(
    uint32_t cpu,
    TraceBlobView event,
    RefPtr<PacketSequenceStateGeneration> state) {
  // Special handling of valid gpu_work_period tracepoint events which contain
  // timestamp values for the GPU time period nested inside the event data.
  auto ts_field = GetFtraceEventField(
      protos::pbzero::FtraceEvent::kGpuWorkPeriodFieldNumber, event);
  if (!ts_field.has_value())
    return;

  protos::pbzero::GpuWorkPeriodFtraceEvent::Decoder gpu_work_event(
      ts_field.value().data(), ts_field.value().size());
  if (!gpu_work_event.has_start_time_ns()) {
    context_->storage->IncrementStats(stats::ftrace_bundle_tokenizer_errors);
    return;
  }
  uint64_t raw_timestamp = gpu_work_event.start_time_ns();

  // Enforce clock type for the event data to be CLOCK_MONOTONIC_RAW
  // as specified, to calculate the timestamp correctly.
  std::optional<int64_t> timestamp = context_->clock_tracker->ToTraceTime(
      ClockId::Machine(BuiltinClock::BUILTIN_CLOCK_MONOTONIC_RAW),
      static_cast<int64_t>(raw_timestamp));

  // ClockTracker will increment some error stats if it failed to convert the
  // timestamp so just return.
  if (!timestamp.has_value()) {
    return;
  }
  module_context_->PushFtraceEvent(
      cpu, *timestamp, TracePacketData{std::move(event), std::move(state)});
}

void FtraceTokenizer::TokenizeFtraceThermalExynosAcpmBulk(
    uint32_t cpu,
    TraceBlobView event,
    RefPtr<PacketSequenceStateGeneration> state) {
  // Special handling of valid thermal_exynos_acpm_bulk tracepoint events which
  // contains the right timestamp value nested inside the event data.
  auto ts_field = GetFtraceEventField(
      protos::pbzero::FtraceEvent::kThermalExynosAcpmBulkFieldNumber, event);
  if (!ts_field.has_value())
    return;

  protos::pbzero::ThermalExynosAcpmBulkFtraceEvent::Decoder
      thermal_exynos_acpm_bulk_event(ts_field.value().data(),
                                     ts_field.value().size());
  if (!thermal_exynos_acpm_bulk_event.has_timestamp()) {
    context_->storage->IncrementStats(stats::ftrace_bundle_tokenizer_errors);
    return;
  }
  auto timestamp =
      static_cast<int64_t>(thermal_exynos_acpm_bulk_event.timestamp());
  module_context_->PushFtraceEvent(
      cpu, timestamp, TracePacketData{std::move(event), std::move(state)});
}

void FtraceTokenizer::TokenizeFtraceParamSetValueCpm(
    uint32_t cpu,
    TraceBlobView event,
    RefPtr<PacketSequenceStateGeneration> state) {
  // Special handling of valid param_set_value_cpm tracepoint events which
  // contains the right timestamp value nested inside the event data.
  auto ts_field = GetFtraceEventField(
      protos::pbzero::FtraceEvent::kParamSetValueCpmFieldNumber, event);
  if (!ts_field.has_value())
    return;

  protos::pbzero::ParamSetValueCpmFtraceEvent::Decoder
      param_set_value_cpm_event(ts_field.value().data(),
                                ts_field.value().size());
  if (!param_set_value_cpm_event.has_timestamp()) {
    context_->storage->IncrementStats(stats::ftrace_bundle_tokenizer_errors);
    return;
  }
  int64_t timestamp = param_set_value_cpm_event.timestamp();
  module_context_->PushFtraceEvent(
      cpu, timestamp, TracePacketData{std::move(event), std::move(state)});
}

void FtraceTokenizer::TokenizeFtraceFwtpPerfettoCounter(
    uint32_t cpu,
    TraceBlobView event,
    RefPtr<PacketSequenceStateGeneration> state) {
  // Special handling of valid fwtp_perfetto_counter tracepoint events which
  // contains the right timestamp value nested inside the event data.
  auto ts_field = GetFtraceEventField(
      protos::pbzero::FtraceEvent::kFwtpPerfettoCounterFieldNumber, event);
  if (!ts_field.has_value())
    return;

  protos::pbzero::FwtpPerfettoCounterFtraceEvent::Decoder
      fwtp_perfetto_counter_event(ts_field.value().data(),
                                  ts_field.value().size());
  if (!fwtp_perfetto_counter_event.has_timestamp()) {
    context_->storage->IncrementStats(stats::ftrace_bundle_tokenizer_errors);
    return;
  }
  int64_t timestamp =
      static_cast<int64_t>(fwtp_perfetto_counter_event.timestamp());
  module_context_->PushFtraceEvent(
      cpu, timestamp, TracePacketData{std::move(event), std::move(state)});
}

void FtraceTokenizer::TokenizeFtraceFwtpPerfettoSlice(
    uint32_t cpu,
    TraceBlobView event,
    RefPtr<PacketSequenceStateGeneration> state) {
  // Special handling of valid fwtp_perfetto_slice tracepoint events which
  // contains the right timestamp value nested inside the event data.
  auto ts_field = GetFtraceEventField(
      protos::pbzero::FtraceEvent::kFwtpPerfettoSliceFieldNumber, event);
  if (!ts_field.has_value())
    return;

  protos::pbzero::FwtpPerfettoSliceFtraceEvent::Decoder
      fwtp_perfetto_slice_event(ts_field.value().data(),
                                ts_field.value().size());
  if (!fwtp_perfetto_slice_event.has_timestamp()) {
    context_->storage->IncrementStats(stats::ftrace_bundle_tokenizer_errors);
    return;
  }
  int64_t timestamp =
      static_cast<int64_t>(fwtp_perfetto_slice_event.timestamp());
  module_context_->PushFtraceEvent(
      cpu, timestamp, TracePacketData{std::move(event), std::move(state)});
}

std::optional<protozero::Field> FtraceTokenizer::GetFtraceEventField(
    uint32_t event_id,
    const TraceBlobView& event) {
  //  Extract ftrace event field by decoding event trace blob.
  const uint8_t* data = event.data();
  const size_t length = event.length();

  ProtoDecoder decoder(data, length);
  auto ts_field = decoder.FindField(event_id);
  if (!ts_field.valid()) {
    context_->storage->IncrementStats(stats::ftrace_bundle_tokenizer_errors);
    return std::nullopt;
  }
  return ts_field;
}

}  // namespace perfetto::trace_processor
