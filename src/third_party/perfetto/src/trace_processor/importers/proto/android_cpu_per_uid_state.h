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

#ifndef SRC_TRACE_PROCESSOR_IMPORTERS_PROTO_ANDROID_CPU_PER_UID_STATE_H_
#define SRC_TRACE_PROCESSOR_IMPORTERS_PROTO_ANDROID_CPU_PER_UID_STATE_H_

#include <cstdint>
#include <string>

#include "perfetto/ext/base/flat_hash_map.h"
#include "src/trace_processor/importers/proto/packet_sequence_state_generation.h"

#include "protos/perfetto/trace/trace_packet.pbzero.h"
#include "src/trace_processor/types/trace_processor_context.h"

namespace perfetto::trace_processor {

struct AndroidCpuPerUidState : PacketSequenceStateGeneration::CustomState {
  explicit AndroidCpuPerUidState(TraceProcessorContext*);
  ~AndroidCpuPerUidState() override;

  uint32_t cluster_count;

  // Key is 32 bits of UID in MSB, 32 bits cluster in LSB.
  base::FlatHashMap<uint64_t, uint64_t> last_values;
};

}  // namespace perfetto::trace_processor

#endif  // SRC_TRACE_PROCESSOR_IMPORTERS_PROTO_ANDROID_CPU_PER_UID_STATE_H_
