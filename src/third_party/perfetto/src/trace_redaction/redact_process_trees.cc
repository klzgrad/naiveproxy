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

#include "src/trace_redaction/redact_process_trees.h"

#include <string>

#include "perfetto/base/status.h"
#include "perfetto/ext/base/status_macros.h"
#include "perfetto/protozero/field.h"
#include "perfetto/protozero/scattered_heap_buffer.h"
#include "src/trace_redaction/proto_util.h"
#include "src/trace_redaction/trace_redaction_framework.h"

#include "protos/perfetto/trace/ps/process_tree.pbzero.h"
#include "protos/perfetto/trace/trace_packet.pbzero.h"

namespace perfetto::trace_redaction {

ProcessTreeModifier::~ProcessTreeModifier() = default;

base::Status ProcessTreeDoNothing::Modify(const Context&,
                                          protos::pbzero::ProcessTree*) const {
  return base::OkStatus();
}

base::Status ProcessTreeCreateSynthThreads::Modify(
    const Context& context,
    protos::pbzero::ProcessTree* message) const {
  PERFETTO_DCHECK(message);

  if (!context.synthetic_process) {
    return base::ErrStatus(
        "ProcessTreeCreateSynthThreads: missing synthetic thread group");
  }

  const auto& tids = context.synthetic_process->tids();

  // At the very least there needs to be a main thread and one CPU thread. If
  // not, something is wrong.
  if (tids.size() < 2) {
    return base::ErrStatus(
        "ProcessTreeCreateSynthThreads: missing synthetic threads");
  }

  auto it = tids.begin();

  auto* process = message->add_processes();
  process->set_uid(context.synthetic_process->uid());
  process->set_ppid(context.synthetic_process->ppid());
  process->set_pid(*it);
  process->add_cmdline("Other-Processes");

  ++it;

  for (; it != tids.end(); ++it) {
    auto name = std::to_string(*it);
    name.insert(0, "cpu-");

    auto* thread = message->add_threads();
    thread->set_tgid(context.synthetic_process->tgid());
    thread->set_tid(*it);
    thread->set_name(name);
  }

  return base::OkStatus();
}

base::Status RedactProcessTrees::Transform(const Context& context,
                                           std::string* packet) const {
  PERFETTO_DCHECK(packet);

  if (!context.package_uid.has_value()) {
    return base::ErrStatus("RedactProcessTrees: missing package uid.");
  }

  if (!context.timeline) {
    return base::ErrStatus("RedactProcessTrees: missing timeline.");
  }

  if (!context.synthetic_process) {
    return base::ErrStatus("RedactProcessTrees: missing synthentic threads.");
  }

  protozero::ProtoDecoder decoder(*packet);

  auto tree =
      decoder.FindField(protos::pbzero::TracePacket::kProcessTreeFieldNumber);

  if (!tree.valid()) {
    return base::OkStatus();
  }

  // This has been verified by the verify primitive.
  auto timestamp =
      decoder.FindField(protos::pbzero::TracePacket::kTimestampFieldNumber);

  protozero::HeapBuffered<protos::pbzero::TracePacket> message;

  for (auto it = decoder.ReadField(); it.valid(); it = decoder.ReadField()) {
    if (it.id() == tree.id()) {
      RETURN_IF_ERROR(OnProcessTree(context, timestamp.as_uint64(),
                                    it.as_bytes(),
                                    message->set_process_tree()));
    } else {
      proto_util::AppendField(it, message.get());
    }
  }

  packet->assign(message.SerializeAsString());

  return base::OkStatus();
}

base::Status RedactProcessTrees::OnProcessTree(
    const Context& context,
    uint64_t ts,
    protozero::ConstBytes bytes,
    protos::pbzero::ProcessTree* message) const {
  protozero::ProtoDecoder decoder(bytes);

  for (auto it = decoder.ReadField(); it.valid(); it = decoder.ReadField()) {
    switch (it.id()) {
      case protos::pbzero::ProcessTree::kProcessesFieldNumber:
        RETURN_IF_ERROR(OnProcess(context, ts, it, message));
        break;
      case protos::pbzero::ProcessTree::kThreadsFieldNumber:
        RETURN_IF_ERROR(OnThread(context, ts, it, message));
        break;
      default:
        proto_util::AppendField(it, message);
        break;
    }
  }

  PERFETTO_DCHECK(modifier_);
  return modifier_->Modify(context, message);
}

base::Status RedactProcessTrees::OnProcess(
    const Context& context,
    uint64_t ts,
    protozero::Field field,
    protos::pbzero::ProcessTree* message) const {
  protozero::ProtoDecoder decoder(field.as_bytes());

  auto pid =
      decoder.FindField(protos::pbzero::ProcessTree::Process::kPidFieldNumber);
  if (!pid.valid()) {
    return base::ErrStatus("RedactProcessTrees: process with no pid");
  }

  PERFETTO_DCHECK(filter_);

  if (filter_->Includes(context, ts, pid.as_int32())) {
    proto_util::AppendField(field, message);
  }

  return base::OkStatus();
}

base::Status RedactProcessTrees::OnThread(
    const Context& context,
    uint64_t ts,
    protozero::Field field,
    protos::pbzero::ProcessTree* message) const {
  protozero::ProtoDecoder decoder(field.as_bytes());

  auto tid =
      decoder.FindField(protos::pbzero::ProcessTree::Thread::kTidFieldNumber);
  if (!tid.valid()) {
    return base::ErrStatus("RedactProcessTrees: thread with no tid");
  }

  PERFETTO_DCHECK(filter_);

  if (filter_->Includes(context, ts, tid.as_int32())) {
    proto_util::AppendField(field, message);
  }

  return base::OkStatus();
}

}  // namespace perfetto::trace_redaction
