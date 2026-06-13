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

#ifndef SRC_TRACE_PROCESSOR_IMPORTERS_PROTO_TRACK_EVENT_SEQUENCE_STATE_H_
#define SRC_TRACE_PROCESSOR_IMPORTERS_PROTO_TRACK_EVENT_SEQUENCE_STATE_H_

#include <cstdint>

#include "perfetto/base/logging.h"
#include "perfetto/ext/base/flat_hash_map.h"
#include "src/trace_processor/importers/proto/packet_sequence_state_generation.h"

namespace perfetto::trace_processor {

class TraceProcessorContext;

// Track-event delta state for one packet sequence: the running reference
// timestamps used to resolve delta-encoded TrackEvent fields, and per-counter
// running values for incremental counters.
//
// Held as a CustomState — i.e. shared across `trace_packet_defaults`
// transitions and reset on `SEQ_INCREMENTAL_STATE_CLEARED`. Surviving
// trace_packet_defaults is the right thing: incremental counter values are
// keyed by track_uuid (stable across defaults), and timestamp deltas remain
// valid as long as the producer doesn't change `timestamp_clock_id` (which it
// is allowed to do but is expected to re-emit a ThreadDescriptor afterwards).
//
// The persistent half (pid/tid) lives in TrackEventThreadDescriptor, which is
// a direct member of `PacketSequenceStateGeneration` because it must survive
// SEQ_INCREMENTAL_STATE_CLEARED too.
class TrackEventSequenceState final
    : public PacketSequenceStateGeneration::CustomState {
 public:
  explicit TrackEventSequenceState(TraceProcessorContext*) {}

  ~TrackEventSequenceState() override;

  // Delta-encoded values (running reference timestamps and incremental
  // counter values) are tied to the run of packets that just got lost; the
  // contract is that the producer re-establishes state before resuming
  // incremental data. Discard our state so the next ThreadDescriptor (or
  // SEQ_INCREMENTAL_STATE_CLEARED) starts from a clean slate.
  bool ClearOnPacketLoss() const override { return true; }

  bool timestamps_valid() const { return timestamps_valid_; }

  // Sets the running reference timestamps from a ThreadDescriptor packet.
  void SetReferenceTimestamps(int64_t timestamp_ns,
                              int64_t thread_timestamp_ns,
                              int64_t thread_instruction_count) {
    timestamps_valid_ = true;
    timestamp_ns_ = timestamp_ns;
    thread_timestamp_ns_ = thread_timestamp_ns;
    thread_instruction_count_ = thread_instruction_count;
  }

  int64_t IncrementAndGetTrackEventTimeNs(int64_t delta_ns) {
    PERFETTO_DCHECK(timestamps_valid());
    timestamp_ns_ += delta_ns;
    return timestamp_ns_;
  }

  int64_t IncrementAndGetTrackEventThreadTimeNs(int64_t delta_ns) {
    PERFETTO_DCHECK(timestamps_valid());
    thread_timestamp_ns_ += delta_ns;
    return thread_timestamp_ns_;
  }

  int64_t IncrementAndGetTrackEventThreadInstructionCount(int64_t delta) {
    PERFETTO_DCHECK(timestamps_valid());
    thread_instruction_count_ += delta;
    return thread_instruction_count_;
  }

  double IncrementAndGetCounterValue(uint64_t counter_track_uuid,
                                     double value) {
    auto [it, inserted] =
        incremental_counter_values_.Insert(counter_track_uuid, 0.0);
    *it += value;
    return *it;
  }

 private:
  // We can only consider TrackEvent delta timestamps to be correct after we
  // have observed a thread descriptor (since the last packet loss).
  bool timestamps_valid_ = false;

  // Current wall/thread timestamps/counters used as reference for the next
  // TrackEvent delta timestamp.
  int64_t timestamp_ns_ = 0;
  int64_t thread_timestamp_ns_ = 0;
  int64_t thread_instruction_count_ = 0;

  base::FlatHashMap<uint64_t /* uuid */, double /* value */>
      incremental_counter_values_;
};

}  // namespace perfetto::trace_processor

#endif  // SRC_TRACE_PROCESSOR_IMPORTERS_PROTO_TRACK_EVENT_SEQUENCE_STATE_H_
