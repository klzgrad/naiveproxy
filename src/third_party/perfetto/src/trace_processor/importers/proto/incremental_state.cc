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

#include "src/trace_processor/importers/proto/incremental_state.h"

#include <cstddef>
#include <cstdint>

#include "perfetto/base/logging.h"
#include "perfetto/protozero/proto_decoder.h"
#include "perfetto/trace_processor/trace_blob_view.h"
#include "src/trace_processor/storage/stats.h"
#include "src/trace_processor/storage/trace_storage.h"
#include "src/trace_processor/types/trace_processor_context.h"
#include "src/trace_processor/util/interned_message_view.h"

namespace perfetto::trace_processor {

CustomState::~CustomState() = default;

// static
RefPtr<IncrementalState> IncrementalState::CreateSuccessor(
    TraceProcessorContext* context,
    TrackEventThreadDescriptor thread_descriptor) {
  auto incr = RefPtr<IncrementalState>(new IncrementalState(context));
  incr->thread_descriptor_ = std::move(thread_descriptor);
  return incr;
}

// static
RefPtr<IncrementalState> IncrementalState::CreateAfterPacketLoss(
    const IncrementalState& prev) {
  auto incr = RefPtr<IncrementalState>(new IncrementalState(prev.context_));
  incr->interned_data_ = prev.interned_data_;
  incr->thread_descriptor_ = prev.thread_descriptor_;
  // Share non-opt-in CustomStates with |prev| (RefPtr copy). Opt-in slots
  // (ClearOnPacketLoss() == true) start empty in the new IS and will be
  // lazy-recreated; the original stays in |prev| for buffered pre-loss
  // packets.
  incr->custom_state_ = prev.custom_state_;
  for (auto& cs : incr->custom_state_) {
    if (cs && cs->ClearOnPacketLoss())
      cs.reset();
  }
  return incr;
}

InternedMessageView* IncrementalState::GetInternedMessageView(uint32_t field_id,
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

void IncrementalState::InternMessage(uint32_t field_id, TraceBlobView message) {
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
