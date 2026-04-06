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

#ifndef SRC_SHARED_LIB_TRACK_EVENT_SERIALIZATION_H_
#define SRC_SHARED_LIB_TRACK_EVENT_SERIALIZATION_H_

#include "perfetto/tracing/internal/track_event_internal.h"
#include "perfetto/tracing/trace_writer_base.h"
#include "src/shared_lib/track_event/ds.h"

namespace perfetto::shlib {

protozero::MessageHandle<perfetto::protos::pbzero::TracePacket>
NewTracePacketInternal(perfetto::TraceWriterBase* trace_writer,
                       perfetto::shlib::TrackEventIncrementalState* incr_state,
                       const perfetto::shlib::TrackEventTlsState& tls_state,
                       perfetto::TraceTimestamp timestamp,
                       uint32_t seq_flags);

void ResetIncrementalStateIfRequired(
    perfetto::TraceWriterBase* trace_writer,
    perfetto::shlib::TrackEventIncrementalState* incr_state,
    const perfetto::shlib::TrackEventTlsState& tls_state,
    const perfetto::TraceTimestamp& timestamp);

}  // namespace perfetto::shlib

#endif  // SRC_SHARED_LIB_TRACK_EVENT_SERIALIZATION_H_
