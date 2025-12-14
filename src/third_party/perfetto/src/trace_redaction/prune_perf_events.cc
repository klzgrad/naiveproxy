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

#include "src/trace_redaction/prune_perf_events.h"

#include "perfetto/protozero/field.h"
#include "perfetto/protozero/scattered_heap_buffer.h"

#include "perfetto/base/logging.h"
#include "perfetto/base/status.h"
#include "perfetto/ext/base/status_macros.h"
#include "src/trace_redaction/proto_util.h"
#include "src/trace_redaction/trace_redaction_framework.h"

#include "protos/perfetto/trace/profiling/profile_packet.pbzero.h"

using namespace perfetto::trace_processor;
namespace perfetto::trace_redaction {

base::Status PrunePerfEvents::Transform(const Context& context,
                                        std::string* packet) const {
  protos::pbzero::TracePacket::Decoder packet_decoder(*packet);
  if (!packet_decoder.has_perf_sample()) {
    // No perf samples found, skip.
    return base::OkStatus();
  }

  protozero::HeapBuffered<protos::pbzero::TracePacket> message;

  std::optional<int64_t> trace_packet_clock_id;
  std::optional<int64_t> trusted_packet_sequence_id;
  if (PERFETTO_UNLIKELY(packet_decoder.has_timestamp_clock_id())) {
    // A clock id was overriden for the packet.
    trace_packet_clock_id =
        static_cast<int64_t>(packet_decoder.timestamp_clock_id());
  } else {
    // No clock if provided, we need to use the trace defaults. Find the
    // corresponding trusted sequence id to identify the correct clock.
    if (packet_decoder.has_trusted_packet_sequence_id()) {
      trusted_packet_sequence_id =
          static_cast<int64_t>(packet_decoder.trusted_packet_sequence_id());
    }
  }

  if (!packet_decoder.has_timestamp()) {
    return base::ErrStatus(
        "PrunePerfEvents: missing field (TracePacket::kTimestamp)");
  }
  auto ts = packet_decoder.timestamp();

  protozero::ProtoDecoder packet_proto_decoder(*packet);

  // Iterate through each field to build the new TracePacket and prune perf
  // samples if required.
  for (auto field = packet_proto_decoder.ReadField(); field.valid();
       field = packet_proto_decoder.ReadField()) {
    if (field.id() == protos::pbzero::TracePacket::kPerfSampleFieldNumber) {
      RETURN_IF_ERROR(OnPerfSample(context, ts, trace_packet_clock_id,
                                   trusted_packet_sequence_id, field,
                                   message.get()));
    } else {
      proto_util::AppendField(field, message.get());
    }
  }

  packet->assign(message.SerializeAsString());

  return base::OkStatus();
}

base::Status PrunePerfEvents::OnPerfSample(
    const Context& context,
    uint64_t ts,
    std::optional<int64_t> trace_packet_clock_id,
    std::optional<int64_t> trusted_packet_sequence_id,
    protozero::Field& perf_sample_field,
    protos::pbzero::TracePacket* message) const {
  protos::pbzero::PerfSample::Decoder decoder(perf_sample_field.as_bytes());
  if (!decoder.has_pid()) {
    // PID is required to compute belongs-to process relationship, however,
    // it is possible for samples to not have a PID such as data loss
    // and profiler global information packets. So, we should make sure
    // any service events (non-pid packets) are retained for the proper working
    // of the profiler. When new service events are added, we should make sure
    // to include them here so the redactor accounts for them.
    if (decoder.has_kernel_records_lost() || decoder.has_producer_event()) {
      proto_util::AppendField(perf_sample_field, message);
      return base::OkStatus();
    } else {
      return base::ErrStatus(
          "PrunePerfEvents: missing field (PerfSample::kPid)");
    }
  }
  int32_t pid = static_cast<int32_t>(decoder.pid());

  uint64_t trace_ts;
  ClockId clock_id;
  if (PERFETTO_LIKELY(!trace_packet_clock_id.has_value())) {
    // No override provided, so grab the default clock for this sequence id.
    if (!trusted_packet_sequence_id.has_value()) {
      return base::ErrStatus(
          "PrunePerfEvents: missing field "
          "(TracePacket::kTrustedPacketSequenceId) in perf sample"
          " which is required to retrieve per data source clocks.");
    }
    ASSIGN_OR_RETURN(
        clock_id, context.clock_converter.GetDataSourceClock(
                      static_cast<uint32_t>(trusted_packet_sequence_id.value()),
                      RedactorClockConverter::DataSourceType::kPerfDataSource));
  } else {
    clock_id = trace_packet_clock_id.value();
  }

  ASSIGN_OR_RETURN(trace_ts,
                   context.clock_converter.ConvertToTrace(clock_id, ts));
  if (filter_->Includes(context, trace_ts, pid)) {
    proto_util::AppendField(perf_sample_field, message);
  }

  return base::OkStatus();
}

}  // namespace perfetto::trace_redaction
