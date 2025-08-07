/*
 * Copyright (C) 2024 The Android Open Source Projectf
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

#include "src/trace_redaction/verify_integrity.h"

#include "perfetto/ext/base/status_macros.h"

#include "protos/perfetto/common/trace_stats.pbzero.h"
#include "protos/perfetto/trace/ftrace/ftrace_event.pbzero.h"
#include "protos/perfetto/trace/ftrace/ftrace_event_bundle.pbzero.h"
#include "protos/perfetto/trace/trace_packet.pbzero.h"

namespace perfetto::trace_redaction {

base::Status VerifyIntegrity::Collect(
    const protos::pbzero::TracePacket::Decoder& packet,
    Context*) const {
  if (!packet.has_trusted_uid()) {
    return base::ErrStatus(
        "VerifyIntegrity: missing field (TracePacket::kTrustedUid).");
  }

  if (packet.trusted_uid() > Context::kMaxTrustedUid) {
    return base::ErrStatus("VerifyIntegrity: untrusted uid found (uid = %d).",
                           packet.trusted_uid());
  }

  if (packet.has_ftrace_events()) {
    RETURN_IF_ERROR(OnFtraceEvents(packet.ftrace_events()));
  }

  // If there is a process tree, there should be a timestamp on the packet. This
  // is the only way to know when the process tree was collected.
  if (packet.has_process_tree() && !packet.has_timestamp()) {
    return base::ErrStatus(
        "VerifyIntegrity: missing fields (TracePacket::kProcessTree + "
        "TracePacket::kTimestamp).");
  }

  // If there are a process stats, there should be a timestamp on the packet.
  // This is the only way to know when the stats were collected.
  if (packet.has_process_stats() && !packet.has_timestamp()) {
    return base::ErrStatus(
        "VerifyIntegrity: missing fields (TracePacket::kProcessStats + "
        "TracePacket::kTimestamp).");
  }

  if (packet.has_trace_stats()) {
    RETURN_IF_ERROR(OnTraceStats(packet.trace_stats()));
  }

  return base::OkStatus();
}

base::Status VerifyIntegrity::OnFtraceEvents(
    const protozero::ConstBytes bytes) const {
  protos::pbzero::FtraceEventBundle::Decoder events(bytes);

  // Any ftrace lost events should cause the trace to be dropped:
  // protos/perfetto/trace/ftrace/ftrace_event_bundle.proto
  if (events.has_lost_events() && events.has_lost_events()) {
    return base::ErrStatus(
        "VerifyIntegrity: detected FtraceEventBundle error.");
  }

  // The other clocks in ftrace are only used on very old kernel versions. No
  // device with V should have such an old version. As a failsafe though,
  // check that the ftrace_clock field is unset to ensure no invalid
  // timestamps get by.
  if (events.has_ftrace_clock()) {
    return base::ErrStatus(
        "VerifyIntegrity: unexpected field (FtraceEventBundle::kFtraceClock).");
  }

  // Every ftrace event bundle should have a CPU field. This is necessary for
  // switch/waking redaction to work.
  if (!events.has_cpu()) {
    return base::ErrStatus(
        "VerifyIntegrity: missing field (FtraceEventBundle::kCpu).");
  }

  // Any ftrace errors should cause the trace to be dropped:
  // protos/perfetto/trace/ftrace/ftrace_event_bundle.proto
  if (events.has_error()) {
    return base::ErrStatus("VerifyIntegrity: detected FtraceEvent errors.");
  }

  for (auto it = events.event(); it; ++it) {
    RETURN_IF_ERROR(OnFtraceEvent(*it));
  }

  return base::OkStatus();
}

base::Status VerifyIntegrity::OnFtraceEvent(
    const protozero::ConstBytes bytes) const {
  protos::pbzero::FtraceEvent::Decoder event(bytes);

  if (!event.has_timestamp()) {
    return base::ErrStatus(
        "VerifyIntegrity: missing field (FtraceEvent::kTimestamp).");
  }

  if (!event.has_pid()) {
    return base::ErrStatus(
        "VerifyIntegrity: missing field (FtraceEvent::kPid).");
  }

  return base::OkStatus();
}

base::Status VerifyIntegrity::OnTraceStats(
    const protozero::ConstBytes bytes) const {
  protos::pbzero::TraceStats::Decoder trace_stats(bytes);

  if (trace_stats.has_flushes_failed() && trace_stats.flushes_failed()) {
    return base::ErrStatus("VerifyIntegrity: detected TraceStats flush fails.");
  }

  if (trace_stats.has_final_flush_outcome() &&
      trace_stats.final_flush_outcome() ==
          protos::pbzero::TraceStats::FINAL_FLUSH_FAILED) {
    return base::ErrStatus(
        "VerifyIntegrity: TraceStats final_flush_outcome is "
        "FINAL_FLUSH_FAILED.");
  }

  for (auto it = trace_stats.buffer_stats(); it; ++it) {
    RETURN_IF_ERROR(OnBufferStats(*it));
  }

  return base::OkStatus();
}

base::Status VerifyIntegrity::OnBufferStats(
    const protozero::ConstBytes bytes) const {
  protos::pbzero::TraceStats::BufferStats::Decoder stats(bytes);

  if (stats.has_patches_failed() && stats.patches_failed()) {
    return base::ErrStatus(
        "VerifyIntegrity: detected BufferStats patch fails.");
  }

  if (stats.has_abi_violations() && stats.abi_violations()) {
    return base::ErrStatus(
        "VerifyIntegrity: detected BufferStats abi violations.");
  }

  auto has_loss = stats.has_trace_writer_packet_loss();
  auto value = stats.trace_writer_packet_loss();

  if (has_loss && value) {
    return base::ErrStatus(
        "VerifyIntegrity: detected BufferStats writer packet loss.");
  }

  return base::OkStatus();
}

}  // namespace perfetto::trace_redaction
