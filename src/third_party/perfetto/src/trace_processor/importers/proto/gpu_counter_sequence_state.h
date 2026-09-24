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

#ifndef SRC_TRACE_PROCESSOR_IMPORTERS_PROTO_GPU_COUNTER_SEQUENCE_STATE_H_
#define SRC_TRACE_PROCESSOR_IMPORTERS_PROTO_GPU_COUNTER_SEQUENCE_STATE_H_

#include <cstdint>

#include "perfetto/ext/base/flat_hash_map.h"
#include "src/trace_processor/importers/proto/packet_sequence_state_generation.h"
#include "src/trace_processor/storage/trace_storage.h"

namespace perfetto::trace_processor {

class TraceProcessorContext;

// Per-incremental-state result of parsing interned GPU counter descriptors.
// The descriptors are parsed once at tokenization time (tracks interned,
// counter groups inserted) and the resulting `counter_id -> track` mapping is
// cached here for the parse stage to look up.
//
// Lives on `IncrementalState` so it shares scope with the interned-data table
// that the descriptors themselves are looked up from — keying by iid alone is
// safe here because two distinct producers necessarily live on distinct packet
// sequences (and therefore distinct IncrementalStates), so they each get their
// own cache.
struct GpuCounterSequenceState : PacketSequenceStateGeneration::CustomState {
  explicit GpuCounterSequenceState(TraceProcessorContext*);
  ~GpuCounterSequenceState() override;

  struct CounterTrackInfo {
    TrackId track_id;
    bool forwards_looking;
  };

  // Key: counter_descriptor_iid. Value: per-descriptor map of counter_id ->
  // track info. Presence of an iid key means the descriptor has already been
  // parsed (tracks interned, groups inserted) at tokenization time.
  base::FlatHashMap<uint64_t, base::FlatHashMap<uint32_t, CounterTrackInfo>>
      descriptors;
};

}  // namespace perfetto::trace_processor

#endif  // SRC_TRACE_PROCESSOR_IMPORTERS_PROTO_GPU_COUNTER_SEQUENCE_STATE_H_
