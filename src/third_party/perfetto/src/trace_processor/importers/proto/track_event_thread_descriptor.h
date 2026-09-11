/*
 * Copyright (C) 2026 The Android Open Source Project
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

#ifndef SRC_TRACE_PROCESSOR_IMPORTERS_PROTO_TRACK_EVENT_THREAD_DESCRIPTOR_H_
#define SRC_TRACE_PROCESSOR_IMPORTERS_PROTO_TRACK_EVENT_THREAD_DESCRIPTOR_H_

#include <cstdint>

#include "protos/perfetto/trace/track_event/thread_descriptor.pbzero.h"
#include "src/trace_processor/importers/common/synthetic_tid.h"

namespace perfetto::trace_processor {

// Persistent pid/tid for a packet sequence, established by a ThreadDescriptor
// packet (or by the equivalent fields on a TrackDescriptor). Held directly on
// `PacketSequenceStateGeneration` because it must survive every transition,
// including SEQ_INCREMENTAL_STATE_CLEARED — producers do not always re-emit
// the descriptor after a state clear and trace_processor relies on the
// previously-known pid/tid persisting.
class TrackEventThreadDescriptor {
 public:
  bool valid() const { return valid_; }
  int32_t pid() const { return pid_; }
  int64_t tid() const {
    return use_synthetic_tid_ ? CreateSyntheticTid(tid_, pid_) : tid_;
  }

  void Set(const protos::pbzero::ThreadDescriptor::Decoder& decoder,
           bool use_synthetic_tid) {
    valid_ = true;
    pid_ = decoder.pid();
    tid_ = decoder.tid();
    use_synthetic_tid_ = use_synthetic_tid;
  }

 private:
  bool valid_ = false;
  int32_t pid_ = 0;
  int64_t tid_ = 0;
  bool use_synthetic_tid_ = false;
};

}  // namespace perfetto::trace_processor

#endif  // SRC_TRACE_PROCESSOR_IMPORTERS_PROTO_TRACK_EVENT_THREAD_DESCRIPTOR_H_
