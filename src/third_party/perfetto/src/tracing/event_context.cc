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

#include "perfetto/tracing/event_context.h"

#include "perfetto/tracing/internal/track_event_interned_fields.h"
#include "protos/perfetto/trace/interned_data/interned_data.pbzero.h"
#include "protos/perfetto/trace/track_event/track_event.pbzero.h"

namespace perfetto {

EventContext::EventContext(
    EventContext::TracePacketHandle trace_packet,
    internal::TrackEventIncrementalState* incremental_state,
    internal::TrackEventTlsState* tls_state)
    : trace_packet_(std::move(trace_packet)),
      event_(trace_packet_->set_track_event()),
      incremental_state_(incremental_state),
      tls_state_(tls_state) {}

EventContext::~EventContext() {
  if (!trace_packet_)
    return;

  // When the track event is finalized (i.e., the context is destroyed), we
  // should flush any newly seen interned data to the trace. The data has
  // earlier been written to a heap allocated protobuf message
  // (|serialized_interned_data|). Here we just need to flush it to the main
  // trace.
  auto& serialized_interned_data = incremental_state_->serialized_interned_data;
  if (PERFETTO_UNLIKELY(!serialized_interned_data.empty())) {
    auto ranges = serialized_interned_data.GetRanges();
    trace_packet_->AppendScatteredBytes(
        perfetto::protos::pbzero::TracePacket::kInternedDataFieldNumber,
        &ranges[0], ranges.size());

    // Reset the message but keep one buffer allocated for future use.
    serialized_interned_data.Reset();
  }
}

protos::pbzero::DebugAnnotation* EventContext::AddDebugAnnotation(
    const char* name) {
  auto annotation = event()->add_debug_annotations();
  annotation->set_name_iid(
      internal::InternedDebugAnnotationName::Get(this, name));
  return annotation;
}

protos::pbzero::DebugAnnotation* EventContext::AddDebugAnnotation(
    ::perfetto::DynamicString name) {
  auto annotation = event()->add_debug_annotations();
  annotation->set_name(name.value);
  return annotation;
}

TrackEventTlsStateUserData* EventContext::GetTlsUserData(const void* key) {
  PERFETTO_CHECK(tls_state_);
  PERFETTO_CHECK(key);
  auto it = tls_state_->user_data.find(key);
  if (it != tls_state_->user_data.end()) {
    return it->second.get();
  }
  return nullptr;
}

void EventContext::SetTlsUserData(
    const void* key,
    std::unique_ptr<TrackEventTlsStateUserData> data) {
  PERFETTO_CHECK(tls_state_);
  PERFETTO_CHECK(key);
  tls_state_->user_data[key] = std::move(data);
}

}  // namespace perfetto
