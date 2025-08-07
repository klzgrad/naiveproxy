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

#include "src/trace_processor/importers/proto/track_event_sequence_state.h"

#include "protos/perfetto/trace/track_event/thread_descriptor.pbzero.h"

namespace perfetto::trace_processor {

void TrackEventSequenceState::SetThreadDescriptor(
    const protos::pbzero::ThreadDescriptor::Decoder& decoder) {
  persistent_state_.pid_and_tid_valid = true;
  persistent_state_.pid = decoder.pid();
  persistent_state_.tid = decoder.tid();

  timestamps_valid_ = true;
  timestamp_ns_ = decoder.reference_timestamp_us() * 1000;
  thread_timestamp_ns_ = decoder.reference_thread_time_us() * 1000;
  thread_instruction_count_ = decoder.reference_thread_instruction_count();
}

}  // namespace perfetto::trace_processor
