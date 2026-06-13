/*
 * Copyright (C) 2020 The Android Open Source Project
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

#include "src/trace_processor/importers/proto/packet_sequence_state_generation.h"

#include "src/trace_processor/importers/proto/incremental_state.h"
#include "src/trace_processor/importers/proto/track_event_thread_descriptor.h"

namespace perfetto::trace_processor {

// static
RefPtr<PacketSequenceStateGeneration>
PacketSequenceStateGeneration::CreateFirst(TraceProcessorContext* context) {
  return RefPtr<PacketSequenceStateGeneration>(
      new PacketSequenceStateGeneration(
          RefPtr<IncrementalState>(new IncrementalState(context)),
          /* trace_packet_defaults */ std::nullopt,
          /* is_incremental_state_valid */ false));
}

RefPtr<PacketSequenceStateGeneration>
PacketSequenceStateGeneration::OnPacketLoss() {
  // Don't mutate `this` or its `IncrementalState`: both may be held by the
  // TraceSorter for buffered packets that were tokenized while the sequence
  // was valid. Build a fresh `IncrementalState` (see `CreateAfterPacketLoss`
  // for what it carries forward vs. resets) and clear the validity bit on
  // the new generation.
  return RefPtr<PacketSequenceStateGeneration>(
      new PacketSequenceStateGeneration(
          IncrementalState::CreateAfterPacketLoss(*incremental_state_),
          trace_packet_defaults_,
          /* is_incremental_state_valid */ false));
}

RefPtr<PacketSequenceStateGeneration>
PacketSequenceStateGeneration::OnIncrementalStateCleared() {
  // SEQ_INCREMENTAL_STATE_CLEARED ends the interval: build a fresh
  // IncrementalState (interned data and custom states reset) but carry the
  // persistent thread descriptor forward.
  return RefPtr<PacketSequenceStateGeneration>(
      new PacketSequenceStateGeneration(
          IncrementalState::CreateSuccessor(
              incremental_state_->context_,
              incremental_state_->thread_descriptor()),
          /* trace_packet_defaults */ std::nullopt,
          /* is_incremental_state_valid */ true));
}

RefPtr<PacketSequenceStateGeneration>
PacketSequenceStateGeneration::OnNewTracePacketDefaults(
    TraceBlobView trace_packet_defaults) {
  // A new defaults blob within the same incremental-state interval. Reuse
  // the existing IncrementalState (same interned data, same CustomStates).
  return RefPtr<PacketSequenceStateGeneration>(
      new PacketSequenceStateGeneration(
          incremental_state_,
          InternedMessageView(std::move(trace_packet_defaults)),
          is_incremental_state_valid_));
}

}  // namespace perfetto::trace_processor
