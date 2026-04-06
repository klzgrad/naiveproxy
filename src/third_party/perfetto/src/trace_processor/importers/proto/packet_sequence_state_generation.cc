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
#include <cstddef>

#include "src/trace_processor/importers/proto/track_event_sequence_state.h"
#include "src/trace_processor/storage/stats.h"
#include "src/trace_processor/storage/trace_storage.h"
#include "src/trace_processor/types/trace_processor_context.h"

namespace perfetto::trace_processor {

PacketSequenceStateGeneration::CustomState::~CustomState() = default;

// static
RefPtr<PacketSequenceStateGeneration>
PacketSequenceStateGeneration::CreateFirst(TraceProcessorContext* context) {
  return RefPtr<PacketSequenceStateGeneration>(
      new PacketSequenceStateGeneration(
          context, TrackEventSequenceState::CreateFirst(), false));
}

PacketSequenceStateGeneration::PacketSequenceStateGeneration(
    TraceProcessorContext* context,
    InternedFieldMap interned_data,
    TrackEventSequenceState track_event_sequence_state,
    CustomStateArray custom_state,
    TraceBlobView trace_packet_defaults,
    bool is_incremental_state_valid)
    : context_(context),
      interned_data_(std::move(interned_data)),
      track_event_sequence_state_(std::move(track_event_sequence_state)),
      custom_state_(std::move(custom_state)),
      trace_packet_defaults_(std::move(trace_packet_defaults)),
      is_incremental_state_valid_(is_incremental_state_valid) {
  for (auto& s : custom_state_) {
    if (s.get() != nullptr) {
      s->set_generation(this);
    }
  }
}

RefPtr<PacketSequenceStateGeneration>
PacketSequenceStateGeneration::OnPacketLoss() {
  // No need to increment the generation. If any future packet depends on
  // previous messages to update the incremental state its packet (if the
  // DataSource is behaving correctly) would have the
  // SEQ_NEEDS_INCREMENTAL_STATE bit set and such a packet will be dropped by
  // the ProtoTraceReader and never make it far enough to update any incremental
  // state.
  track_event_sequence_state_.OnPacketLoss();
  is_incremental_state_valid_ = false;
  return RefPtr<PacketSequenceStateGeneration>(this);
}

RefPtr<PacketSequenceStateGeneration>
PacketSequenceStateGeneration::OnIncrementalStateCleared() {
  return RefPtr<PacketSequenceStateGeneration>(
      new PacketSequenceStateGeneration(
          context_, track_event_sequence_state_.OnIncrementalStateCleared(),
          true));
}

RefPtr<PacketSequenceStateGeneration>
PacketSequenceStateGeneration::OnNewTracePacketDefaults(
    TraceBlobView trace_packet_defaults) {
  return RefPtr<PacketSequenceStateGeneration>(
      new PacketSequenceStateGeneration(
          context_, interned_data_,
          track_event_sequence_state_.OnIncrementalStateCleared(),
          custom_state_, std::move(trace_packet_defaults),
          is_incremental_state_valid_));
}

InternedMessageView* PacketSequenceStateGeneration::GetInternedMessageView(
    uint32_t field_id,
    uint64_t iid) {
  auto field_it = interned_data_.find(field_id);
  if (field_it != interned_data_.end()) {
    auto* message_map = &field_it->second;
    auto it = message_map->find(iid);
    if (it != message_map->end()) {
      return &it->second;
    }
  }

  context_->storage->IncrementStats(stats::interned_data_tokenizer_errors);
  return nullptr;
}

void PacketSequenceStateGeneration::InternMessage(uint32_t field_id,
                                                  TraceBlobView message) {
  constexpr auto kIidFieldNumber = 1;

  uint64_t iid = 0;
  auto message_start = message.data();
  auto message_size = message.length();
  protozero::ProtoDecoder decoder(message_start, message_size);

  auto field = decoder.FindField(kIidFieldNumber);
  if (PERFETTO_UNLIKELY(!field)) {
    PERFETTO_DLOG("Interned message without interning_id");
    context_->storage->IncrementStats(stats::interned_data_tokenizer_errors);
    return;
  }
  iid = field.as_uint64();

  auto res = interned_data_[field_id].emplace(
      iid, InternedMessageView(std::move(message)));

  // If a message with this ID is already interned in the same generation,
  // its data should not have changed (this is forbidden by the InternedData
  // proto).
  // TODO(eseckler): This DCHECK assumes that the message is encoded the
  // same way if it is re-emitted.
  PERFETTO_DCHECK(res.second ||
                  (res.first->second.message().length() == message_size &&
                   memcmp(res.first->second.message().data(), message_start,
                          message_size) == 0));
}

}  // namespace perfetto::trace_processor
