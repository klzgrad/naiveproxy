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

#include "src/trace_redaction/reduce_threads_in_process_trees.h"

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

bool ShouldCopyProcessToProcessTree(const ProcessThreadTimeline& timeline,
                                    uint64_t package,
                                    uint64_t ts,
                                    protozero::ConstBytes src) {
  protos::pbzero::ProcessTree::Process::Decoder decoder(src);
  return decoder.has_pid() &&
         timeline.PidConnectsToUid(ts, decoder.pid(), package);
}

bool ShoudCopyThreadToProcessTree(const ProcessThreadTimeline& timeline,
                                  uint64_t package,
                                  uint64_t ts,
                                  protozero::ConstBytes src) {
  protos::pbzero::ProcessTree::Thread::Decoder decoder(src);
  return decoder.has_tid() &&
         timeline.PidConnectsToUid(ts, decoder.tid(), package);
}

void CopyProcessTree(const ProcessThreadTimeline& timeline,
                     uint64_t package,
                     uint64_t ts,
                     protozero::ConstBytes src,
                     protos::pbzero::ProcessTree* dest) {
  protozero::ProtoDecoder decoder(src);

  for (auto it = decoder.ReadField(); it; it = decoder.ReadField()) {
    switch (it.id()) {
      case protos::pbzero::ProcessTree::kProcessesFieldNumber:
        if (ShouldCopyProcessToProcessTree(timeline, package, ts,
                                           it.as_bytes())) {
          proto_util::AppendField(it, dest);
        }
        break;

      case protos::pbzero::ProcessTree::kThreadsFieldNumber:
        if (ShoudCopyThreadToProcessTree(timeline, package, ts,
                                         it.as_bytes())) {
          proto_util::AppendField(it, dest);
        }
        break;

      default:
        proto_util::AppendField(it, dest);
        break;
    }
  }
}

}  // namespace

base::Status ReduceThreadsInProcessTrees::Transform(const Context& context,
                                                    std::string* packet) const {
  PERFETTO_DCHECK(packet);

  if (!context.package_uid.has_value()) {
    return base::ErrStatus("RedactProcessTrees: missing package uid.");
  }

  if (!context.timeline) {
    return base::ErrStatus("RedactProcessTrees: missing timeline.");
  }

  protozero::ProtoDecoder decoder(*packet);

  // This has been verified by the verify primitive.
  auto timestamp =
      decoder.FindField(protos::pbzero::TracePacket::kTimestampFieldNumber);

  protozero::HeapBuffered<protos::pbzero::TracePacket> message;

  for (auto it = decoder.ReadField(); it.valid(); it = decoder.ReadField()) {
    if (it.id() == protos::pbzero::TracePacket::kProcessTreeFieldNumber) {
      CopyProcessTree(*context.timeline, *context.package_uid,
                      timestamp.as_uint64(), it.as_bytes(),
                      message->set_process_tree());
    } else {
      proto_util::AppendField(it, message.get());
    }
  }

  packet->assign(message.SerializeAsString());

  return base::OkStatus();
}

}  // namespace perfetto::trace_redaction
