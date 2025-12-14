/*
 * Copyright (C) 2025 The Android Open Source Project
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

#include "src/trace_redaction/collect_clocks.h"

#include "perfetto/protozero/field.h"

#include "perfetto/base/status.h"
#include "perfetto/ext/base/status_macros.h"
#include "src/trace_redaction/trace_redaction_framework.h"

#include "protos/perfetto/trace/clock_snapshot.pbzero.h"
#include "protos/perfetto/trace/trace_packet_defaults.pbzero.h"

using namespace perfetto::trace_processor;

namespace perfetto::trace_redaction {

base::Status CollectClocks::Collect(
    const protos::pbzero::TracePacket::Decoder& packet,
    Context* context) const {
  if (packet.has_clock_snapshot()) {
    RETURN_IF_ERROR(ParseClockSnapshot(packet, context));
  } else if (packet.has_trace_packet_defaults()) {
    RETURN_IF_ERROR(ParseTracePacketDefaults(packet, context));
  }

  return base::OkStatus();
}

base::Status CollectClocks::ParseClockSnapshot(
    const protos::pbzero::TracePacket::Decoder& packet,
    Context* context) const {
  PERFETTO_DCHECK(packet.has_clock_snapshot());

  // clock_snapshot_ is used specifically for performance reasons
  // to avoid re-creating a local vector for every packet collection.
  // It is only used by this function and won't impact logic elsewhere,
  // it merely caches the local variable to avoid re-creating it every
  // during every function call.
  // The append-only architecture of Collector is maintained, however,
  // this approach assumes that Collectors are single-threaded.
  clock_snapshot_.clear();

  protos::pbzero::ClockSnapshot::Decoder snapshot_decoder(
      packet.clock_snapshot());

  if (snapshot_decoder.has_primary_trace_clock()) {
    int32_t trace_clock = snapshot_decoder.primary_trace_clock();
    RETURN_IF_ERROR(context->clock_converter.SetTraceClock(
        static_cast<int64_t>(trace_clock)));
  }
  for (auto clock_it = snapshot_decoder.clocks(); clock_it; clock_it++) {
    ASSIGN_OR_RETURN(ClockTimestamp clock_ts, ParseClock(clock_it->as_bytes()));
    clock_snapshot_.push_back(clock_ts);
  }

  RETURN_IF_ERROR(context->clock_converter.AddClockSnapshot(clock_snapshot_));

  return base::OkStatus();
}

base::Status CollectClocks::ParseTracePacketDefaults(
    const protos::pbzero::TracePacket::Decoder& packet,
    Context* context) const {
  PERFETTO_DCHECK(packet.has_trace_packet_defaults());

  if (!packet.has_trusted_packet_sequence_id()) {
    return base::ErrStatus(
        "Could not find sequence id for TracePacketDefaults");
  }
  uint32_t trusted_seq_id = packet.trusted_packet_sequence_id();
  RETURN_IF_ERROR(OnTracePacketDefaults(packet.trace_packet_defaults(),
                                        trusted_seq_id, context));

  return base::OkStatus();
}

base::StatusOr<ClockTimestamp> CollectClocks::ParseClock(
    protozero::ConstBytes clock_bytes) const {
  ClockTimestamp clock_ts(0, 0);
  protos::pbzero::ClockSnapshot_Clock::Decoder clock_decoder(clock_bytes);
  if (!clock_decoder.has_clock_id()) {
    return base::ErrStatus("Could not find clock id in clock snapshot");
  }

  if (!clock_decoder.has_timestamp()) {
    return base::ErrStatus("Could not find clock timestamp in clock snapshot");
  }
  return ClockTimestamp(static_cast<int64_t>(clock_decoder.clock_id()),
                        static_cast<int64_t>(clock_decoder.timestamp()));
}

base::Status CollectClocks::OnTracePacketDefaults(
    protozero::ConstBytes trace_packet_defaults,
    uint32_t trusted_sequence_id,
    Context* context) const {
  protos::pbzero::TracePacketDefaults::Decoder trace_packet_defaults_decoder(
      trace_packet_defaults);
  if (trace_packet_defaults_decoder.has_perf_sample_defaults()) {
    // We have found a packet that defines default clocks for perf data source
    // collect that information as it will be required to convert packets from
    // that data source into trace time to be used by the redactor timeline.
    if (!trace_packet_defaults_decoder.has_timestamp_clock_id()) {
      return base::ErrStatus(
          "Could not find a timestamp in trace packet defaults");
    }
    uint32_t perf_clock_id = trace_packet_defaults_decoder.timestamp_clock_id();
    context->clock_converter.SetDefaultDataSourceClock(
        RedactorClockConverter::DataSourceType::kPerfDataSource, perf_clock_id,
        trusted_sequence_id);
  }

  return base::OkStatus();
}

}  // namespace perfetto::trace_redaction
