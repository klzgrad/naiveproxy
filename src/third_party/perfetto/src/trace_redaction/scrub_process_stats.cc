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

#include "src/trace_redaction/scrub_process_stats.h"

#include <string>

#include "perfetto/base/status.h"
#include "perfetto/ext/base/status_macros.h"
#include "perfetto/protozero/field.h"
#include "perfetto/protozero/scattered_heap_buffer.h"
#include "src/trace_redaction/proto_util.h"
#include "src/trace_redaction/trace_redaction_framework.h"

#include "protos/perfetto/trace/ps/process_stats.pbzero.h"

namespace perfetto::trace_redaction {

base::Status ScrubProcessStats::Transform(const Context& context,
                                          std::string* packet) const {
  if (!context.package_uid.has_value()) {
    return base::ErrStatus("FilterProcessStats: missing package uid.");
  }

  if (!context.timeline) {
    return base::ErrStatus("FilterProcessStats: missing timeline.");
  }

  protozero::ProtoDecoder packet_decoder(*packet);

  // Very few packets will have process stats. It's best to avoid
  // reserialization whenever possible.
  if (!packet_decoder
           .FindField(protos::pbzero::TracePacket::kProcessStatsFieldNumber)
           .valid()) {
    return base::OkStatus();
  }

  protozero::HeapBuffered<protos::pbzero::TracePacket> message;

  // Not all packets will have a top-level timestamp, but for process stats, the
  // timestamp is located at the trace packet.
  auto time_field = packet_decoder.FindField(
      protos::pbzero::TracePacket::kTimestampFieldNumber);
  PERFETTO_DCHECK(time_field.valid());

  auto ts = time_field.as_uint64();

  for (auto field = packet_decoder.ReadField(); field.valid();
       field = packet_decoder.ReadField()) {
    if (field.id() == protos::pbzero::TracePacket::kProcessStatsFieldNumber) {
      RETURN_IF_ERROR(OnProcessStats(context, ts, field.as_bytes(),
                                     message->set_process_stats()));
    } else {
      proto_util::AppendField(field, message.get());
    }
  }

  packet->assign(message.SerializeAsString());

  return base::OkStatus();
}

base::Status ScrubProcessStats::OnProcessStats(
    const Context& context,
    uint64_t ts,
    protozero::ConstBytes bytes,
    protos::pbzero::ProcessStats* message) const {
  protozero::ProtoDecoder decoder(bytes);

  for (auto field = decoder.ReadField(); field.valid();
       field = decoder.ReadField()) {
    if (field.id() == protos::pbzero::ProcessStats::kProcessesFieldNumber) {
      RETURN_IF_ERROR(OnProcess(context, ts, field, message));
    } else {
      proto_util::AppendField(field, message);
    }
  }

  return base::OkStatus();
}

base::Status ScrubProcessStats::OnProcess(
    const Context& context,
    uint64_t ts,
    protozero::Field field,
    protos::pbzero::ProcessStats* message) const {
  PERFETTO_DCHECK(field.id() ==
                  protos::pbzero::ProcessStats::kProcessesFieldNumber);

  protozero::ProtoDecoder decoder(field.as_bytes());
  auto pid =
      decoder.FindField(protos::pbzero::ProcessStats::Process::kPidFieldNumber);
  PERFETTO_DCHECK(pid.valid());

  PERFETTO_DCHECK(filter_);
  if (filter_->Includes(context, ts, pid.as_int32())) {
    proto_util::AppendField(field, message);
  }

  return base::OkStatus();
}

}  // namespace perfetto::trace_redaction
