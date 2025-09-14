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

#include "src/trace_redaction/drop_empty_ftrace_events.h"

#include <string>

#include "perfetto/base/status.h"
#include "perfetto/protozero/scattered_heap_buffer.h"
#include "protos/perfetto/trace/ftrace/ftrace_event.pbzero.h"
#include "protos/perfetto/trace/ftrace/ftrace_event_bundle.pbzero.h"
#include "src/trace_redaction/proto_util.h"
#include "src/trace_redaction/trace_redaction_framework.h"

namespace perfetto::trace_redaction {
namespace {
constexpr auto kPidFieldNumber = protos::pbzero::FtraceEvent::kPidFieldNumber;
constexpr auto kTimestampFieldNumber =
    protos::pbzero::FtraceEvent::kTimestampFieldNumber;
constexpr auto kEventFieldNumber =
    protos::pbzero::FtraceEventBundle::kEventFieldNumber;
constexpr auto kFtraceEventsFieldNumber =
    protos::pbzero::TracePacket::kFtraceEventsFieldNumber;

// Look at a FtraceEvent and determine if it has more than a pid and a
// timestamp. If the event only has a pid and timestamp, it is considered empty
// and should be removed.
bool IsFtraceEventEmpty(protozero::ConstBytes bytes) {
  protozero::ProtoDecoder decoder(bytes);

  for (auto field = decoder.ReadField(); field.valid();
       field = decoder.ReadField()) {
    const auto id = field.id();

    if (id != kPidFieldNumber && id != kTimestampFieldNumber) {
      return false;
    }
  }

  return true;
}

// Look at every field in a ftrace event bundle and remove any empty ftrace
// event messages. It's possible for every ftrace event to be removed. When
// that happens, the ftrace event bundle should be removed. That is out of
// scope for this primitive and should be handled elsewhere.
void OnFtraceEventBundle(
    protozero::ConstBytes bytes,
    protos::pbzero::FtraceEventBundle* ftrace_event_bundle) {
  protozero::ProtoDecoder decoder(bytes);

  for (auto field = decoder.ReadField(); field.valid();
       field = decoder.ReadField()) {
    const auto id = field.id();

    if (id == kEventFieldNumber && IsFtraceEventEmpty(field.as_bytes())) {
      continue;
    }

    proto_util::AppendField(field, ftrace_event_bundle);
  }
}

}  // namespace

base::Status DropEmptyFtraceEvents::Transform(const Context&,
                                              std::string* packet) const {
  protozero::ProtoDecoder decoder(*packet);
  protozero::HeapBuffered<protos::pbzero::TracePacket> packet_message;

  for (auto field = decoder.ReadField(); field.valid();
       field = decoder.ReadField()) {
    if (field.id() == kFtraceEventsFieldNumber) {
      OnFtraceEventBundle(field.as_bytes(),
                          packet_message->set_ftrace_events());
    } else {
      proto_util::AppendField(field, packet_message.get());
    }
  }

  packet->assign(packet_message.SerializeAsString());
  return base::OkStatus();
}

}  // namespace perfetto::trace_redaction
