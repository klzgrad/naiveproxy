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

#include "src/trace_redaction/collect_system_info.h"

#include "perfetto/protozero/field.h"

#include "protos/perfetto/trace/ftrace/ftrace_event_bundle.pbzero.h"
#include "protos/perfetto/trace/ps/process_tree.pbzero.h"

namespace perfetto::trace_redaction {

base::Status CollectSystemInfo::Begin(Context* context) const {
  // Other primitives are allows to push more data into the system info (e.g.
  // another source of pids).
  if (!context->system_info.has_value()) {
    context->system_info.emplace();
  }

  return base::OkStatus();
}

base::Status CollectSystemInfo::Collect(
    const protos::pbzero::TracePacket::Decoder& packet,
    Context* context) const {
  auto* system_info = &context->system_info.value();

  PERFETTO_DCHECK(system_info);  // See Begin()

  if (packet.has_ftrace_events()) {
    return OnFtraceEvents(packet.ftrace_events(), context);
  }

  return base::OkStatus();
}

base::Status CollectSystemInfo::OnFtraceEvents(protozero::ConstBytes bytes,
                                               Context* context) const {
  protozero::ProtoDecoder decoder(bytes);

  auto cpu =
      decoder.FindField(protos::pbzero::FtraceEventBundle::kCpuFieldNumber);

  if (!cpu.valid()) {
    return base::ErrStatus(
        "BuildSyntheticThreads: missing FtraceEventBundle::kCpu.");
  }

  if (cpu.valid()) {
    context->system_info->ReserveCpu(cpu.as_uint32());
  }

  return base::OkStatus();
}

base::Status BuildSyntheticThreads::Build(Context* context) const {
  if (!context->system_info.has_value()) {
    return base::ErrStatus("BuildSyntheticThreads: missing system info.");
  }

  if (context->synthetic_process) {
    return base::ErrStatus(
        "BuildSyntheticThreads: synthetic threads were already initialized.");
  }

  auto& system_info = context->system_info.value();

  // Add an extra tid for the main thread.
  std::vector<int32_t> tids(system_info.cpu_count() + 1);

  for (uint32_t i = 0; i < tids.size(); ++i) {
    tids[i] = system_info.AllocateSynthThread();
  }

  context->synthetic_process = std::make_unique<SyntheticProcess>(tids);

  return base::OkStatus();
}

}  // namespace perfetto::trace_redaction
