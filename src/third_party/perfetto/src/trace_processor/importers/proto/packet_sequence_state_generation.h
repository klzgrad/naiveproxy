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

#ifndef SRC_TRACE_PROCESSOR_IMPORTERS_PROTO_PACKET_SEQUENCE_STATE_GENERATION_H_
#define SRC_TRACE_PROCESSOR_IMPORTERS_PROTO_PACKET_SEQUENCE_STATE_GENERATION_H_

#include <cstdint>
#include <optional>
#include <type_traits>
#include <utility>

#include "perfetto/trace_processor/ref_counted.h"
#include "perfetto/trace_processor/trace_blob_view.h"
#include "src/trace_processor/importers/proto/incremental_state.h"
#include "src/trace_processor/importers/proto/track_event_thread_descriptor.h"
#include "src/trace_processor/util/interned_message_view.h"

#include "protos/perfetto/trace/trace_packet_defaults.pbzero.h"
#include "protos/perfetto/trace/track_event/track_event.pbzero.h"

namespace perfetto::trace_processor {

class TraceProcessorContext;

// Snapshot of per-sequence state at the granularity of one
// `trace_packet_defaults` slice. RefCounted: the TraceSorter pins one of
// these per buffered packet so that, when the packet is later parsed in
// timestamp order, lookups (interned data, custom states, defaults) resolve
// to the state that was current at tokenization time.
//
// The interval-scoped state (interned data, CustomStates, persistent thread
// descriptor) lives in `IncrementalState`, which is shared by RefPtr across
// every Generation produced from the same SEQ_INCREMENTAL_STATE_CLEARED
// interval. A new `IncrementalState` is constructed when the interval
// resets; a new Generation (sharing the same `IncrementalState`) is
// constructed on `trace_packet_defaults` changes.
class PacketSequenceStateGeneration : public RefCounted {
 public:
  // Re-export `CustomState` and the traits machinery so existing call sites
  // (e.g. `state->GetCustomState<T>()`) keep working. The actual ownership
  // and definitions live in `incremental_state.h`.
  using CustomState = ::perfetto::trace_processor::CustomState;

  static RefPtr<PacketSequenceStateGeneration> CreateFirst(
      TraceProcessorContext* context);

  RefPtr<PacketSequenceStateGeneration> OnPacketLoss();
  RefPtr<PacketSequenceStateGeneration> OnIncrementalStateCleared();
  RefPtr<PacketSequenceStateGeneration> OnNewTracePacketDefaults(
      TraceBlobView trace_packet_defaults);

  // Persistent pid/tid for this packet sequence (set by ThreadDescriptor).
  // Carried across every transition (including SEQ_INCREMENTAL_STATE_CLEARED).
  TrackEventThreadDescriptor& thread_descriptor() {
    return incremental_state_->thread_descriptor();
  }
  const TrackEventThreadDescriptor& thread_descriptor() const {
    return incremental_state_->thread_descriptor();
  }

  // Returns |nullptr| if the message with the given |iid| was not found (also
  // records a stat in this case).
  template <uint32_t FieldId, typename MessageType>
  typename MessageType::Decoder* LookupInternedMessage(uint64_t iid) {
    return incremental_state_->LookupInternedMessage<FieldId, MessageType>(iid);
  }
  InternedMessageView* GetInternedMessageView(uint32_t field_id, uint64_t iid) {
    return incremental_state_->GetInternedMessageView(field_id, iid);
  }

  // Returns |nullptr| if no defaults were set.
  InternedMessageView* GetTracePacketDefaultsView() {
    if (!trace_packet_defaults_.has_value()) {
      return nullptr;
    }
    return &*trace_packet_defaults_;
  }

  // Returns |nullptr| if no defaults were set.
  protos::pbzero::TracePacketDefaults::Decoder* GetTracePacketDefaults() {
    if (!trace_packet_defaults_.has_value()) {
      return nullptr;
    }
    return trace_packet_defaults_
        ->GetOrCreateDecoder<protos::pbzero::TracePacketDefaults>();
  }

  // Returns |nullptr| if no TrackEventDefaults were set.
  protos::pbzero::TrackEventDefaults::Decoder* GetTrackEventDefaults() {
    auto* packet_defaults_view = GetTracePacketDefaultsView();
    if (packet_defaults_view) {
      auto* track_event_defaults_view =
          packet_defaults_view
              ->GetOrCreateSubmessageView<protos::pbzero::TracePacketDefaults,
                                          protos::pbzero::TracePacketDefaults::
                                              kTrackEventDefaultsFieldNumber>();
      if (track_event_defaults_view) {
        return track_event_defaults_view
            ->GetOrCreateDecoder<protos::pbzero::TrackEventDefaults>();
      }
    }
    return nullptr;
  }

  // Extension point for custom incremental state. Custom state classes need
  // to inherit from `CustomState` and be listed in `CustomStateClasses`.
  template <typename T, typename... Args>
  std::remove_cv_t<T>* GetCustomState(Args... args) {
    return incremental_state_->GetCustomState<T>(std::forward<Args>(args)...);
  }

  // TODO(carlscab): Nobody other than `ProtoTraceReader` should care about
  // this. Remove.
  bool IsIncrementalStateValid() const { return is_incremental_state_valid_; }

 private:
  friend class PacketSequenceStateBuilder;

  PacketSequenceStateGeneration(
      RefPtr<IncrementalState> incremental_state,
      std::optional<InternedMessageView> trace_packet_defaults,
      bool is_incremental_state_valid)
      : incremental_state_(std::move(incremental_state)),
        trace_packet_defaults_(std::move(trace_packet_defaults)),
        is_incremental_state_valid_(is_incremental_state_valid) {}

  // Add an interned message to the underlying IncrementalState. Only callable
  // by `PacketSequenceStateBuilder` (which is a friend) since packet
  // tokenizers and parsers should never deal directly with reading interned
  // data out of trace packets.
  void InternMessage(uint32_t field_id, TraceBlobView message) {
    incremental_state_->InternMessage(field_id, std::move(message));
  }

  // Shared with sibling generations within the same incremental-state
  // interval (one new IncrementalState is constructed on
  // SEQ_INCREMENTAL_STATE_CLEARED).
  RefPtr<IncrementalState> incremental_state_;

  // Per-slice state.
  std::optional<InternedMessageView> trace_packet_defaults_;
  // TODO(carlscab): Should not be needed as clients of this class should not
  // care about validity.
  bool is_incremental_state_valid_ = true;
};

}  // namespace perfetto::trace_processor

#endif  // SRC_TRACE_PROCESSOR_IMPORTERS_PROTO_PACKET_SEQUENCE_STATE_GENERATION_H_
