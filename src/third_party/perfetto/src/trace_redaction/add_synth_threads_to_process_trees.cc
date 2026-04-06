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

#include "src/trace_redaction/add_synth_threads_to_process_trees.h"

#include <string>

#include "perfetto/base/status.h"
#include "perfetto/protozero/field.h"
#include "perfetto/protozero/scattered_heap_buffer.h"
#include "src/trace_redaction/proto_util.h"
#include "src/trace_redaction/trace_redaction_framework.h"

#include "protos/perfetto/trace/ps/process_tree.pbzero.h"
#include "protos/perfetto/trace/trace_packet.pbzero.h"

namespace perfetto::trace_redaction {
namespace {

void AddProcessToProcessTree(const SyntheticProcess& synthetic_process,
                             protos::pbzero::ProcessTree* process_tree) {
  auto* process = process_tree->add_processes();
  process->set_uid(synthetic_process.uid());
  process->set_ppid(synthetic_process.ppid());
  process->set_pid(synthetic_process.tgid());
  process->add_cmdline("Other-Processes");
}

void AddThreadsToProcessTree(const SyntheticProcess& synthetic_process,
                             protos::pbzero::ProcessTree* process_tree) {
  auto tgid = synthetic_process.tgid();
  const auto& tids = synthetic_process.tids();

  PERFETTO_DCHECK(!tids.empty());

  for (auto tid : tids) {
    auto name = std::to_string(tid);
    name.insert(0, "cpu-");

    auto* thread = process_tree->add_threads();
    thread->set_tgid(tgid);
    thread->set_tid(tid);
    thread->set_name(name);
  }
}
}  // namespace

base::Status AddSythThreadsToProcessTrees::Transform(
    const Context& context,
    std::string* packet) const {
  PERFETTO_DCHECK(packet);

  if (!context.synthetic_process) {
    return base::ErrStatus(
        "AddSythThreadsToProcessTrees: missing synthentic threads.");
  }

  const auto& synthetic_process = *context.synthetic_process;

  if (synthetic_process.tids().empty()) {
    return base::ErrStatus(
        "AddSythThreadsToProcessTrees: no synthentic threads in synthentic "
        "process.");
  }

  protozero::ProtoDecoder decoder(*packet);
  protozero::HeapBuffered<protos::pbzero::TracePacket> message;

  for (auto it = decoder.ReadField(); it.valid(); it = decoder.ReadField()) {
    if (it.id() == protos::pbzero::TracePacket::kProcessTreeFieldNumber) {
      auto* process_tree = message->set_process_tree();

      // Copy fields from one process tree to another process tree.
      proto_util::AppendFields(it, process_tree);

      AddProcessToProcessTree(synthetic_process, process_tree);
      AddThreadsToProcessTree(synthetic_process, process_tree);
    } else {
      proto_util::AppendField(it, message.get());
    }
  }

  packet->assign(message.SerializeAsString());

  return base::OkStatus();
}

}  // namespace perfetto::trace_redaction
