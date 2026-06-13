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

#ifndef SRC_TRACE_PROCESSOR_IMPORTERS_PROTO_TRACK_EVENT_TRACKER_H_
#define SRC_TRACE_PROCESSOR_IMPORTERS_PROTO_TRACK_EVENT_TRACKER_H_

#include <cstdint>
#include <optional>
#include <tuple>
#include <unordered_set>
#include <utility>
#include <variant>

#include "perfetto/base/logging.h"
#include "perfetto/ext/base/flat_hash_map.h"
#include "perfetto/ext/base/variant.h"
#include "src/trace_processor/importers/common/args_tracker.h"
#include "src/trace_processor/importers/common/track_compressor.h"
#include "src/trace_processor/importers/proto/packet_sequence_state_generation.h"
#include "src/trace_processor/storage/trace_storage.h"
#include "src/trace_processor/types/trace_processor_context.h"

namespace perfetto::trace_processor {

// Tracks and stores tracks based on track types, ids and scopes.
class TrackEventTracker {
 public:
  static constexpr uint64_t kDefaultDescriptorTrackUuid = 0u;

  // Data from TrackDescriptor proto used to reserve a track before interning it
  // with |TrackTracker|.
  struct DescriptorTrackReservation {
    // Maps to TrackDescriptor::ChildTracksOrdering proto values
    enum class ChildTracksOrdering {
      kUnknown = 0,
      kLexicographic = 1,
      kChronological = 2,
      kExplicit = 3,
    };
    struct CounterDetails {
      StringId category = kNullStringId;
      int64_t unit_multiplier = 1;
      bool is_incremental = false;
      StringId unit = kNullStringId;
      StringId builtin_type_str;
      StringId y_axis_share_key = kNullStringId;

      bool IsForSameTrack(const CounterDetails& o) const {
        return std::tie(category, unit_multiplier, is_incremental,
                        builtin_type_str, y_axis_share_key) ==
               std::tie(o.category, o.unit_multiplier, o.is_incremental,
                        o.builtin_type_str, o.y_axis_share_key);
      }
    };
    enum class SiblingMergeBehavior {
      kByName = 0,
      kNone = 1,
      kByKey = 2,
    };

    uint64_t parent_uuid = 0;
    std::optional<int64_t> pid;
    std::optional<int64_t> tid;
    int64_t min_timestamp = 0;
    StringId name = kNullStringId;
    StringId description = kNullStringId;
    bool use_separate_track = false;
    bool is_counter = false;
    bool use_synthetic_tid = false;

    // For counter tracks.
    std::optional<CounterDetails> counter_details;

    // For UI visualisation
    ChildTracksOrdering ordering = ChildTracksOrdering::kUnknown;
    std::optional<int32_t> sibling_order_rank;

    // For merging tracks.
    SiblingMergeBehavior sibling_merge_behavior = SiblingMergeBehavior::kByName;
    StringId sibling_merge_key = kNullStringId;

    // Whether |other| is a valid descriptor for this track reservation. A track
    // should always remain nested underneath its original parent.
    bool IsForSameTrack(const DescriptorTrackReservation& other) {
      if (counter_details.has_value() != other.counter_details.has_value()) {
        return false;
      }
      if (counter_details &&
          !counter_details->IsForSameTrack(*other.counter_details)) {
        return false;
      }
      return std::tie(parent_uuid, pid, tid, is_counter, sibling_merge_behavior,
                      sibling_merge_key) ==
             std::tie(other.parent_uuid, other.pid, other.tid, other.is_counter,
                      other.sibling_merge_behavior, other.sibling_merge_key);
    }
  };

  // A descriptor track which has been resolved to a concrete track in the
  // trace.
  class ResolvedDescriptorTrack {
   public:
    // The scope of a descriptor track.
    enum class Scope {
      // This track is associated with a thread.
      kThread,
      // This track is associated with a process.
      kProcess,
      // This track is global.
      kGlobal,
    };

    // Creates a process-scoped resolved descriptor track.
    static ResolvedDescriptorTrack Process(UniquePid upid,
                                           bool is_counter,
                                           bool is_root);
    // Creates a thread-scoped resolved descriptor track.
    static ResolvedDescriptorTrack Thread(UniqueTid utid,
                                          bool is_counter,
                                          bool is_root);

    // Creates a global-scoped resolved descriptor track.
    static ResolvedDescriptorTrack Global(bool is_counter);

    // The scope of the resolved track.
    Scope scope() const { return scope_; }

    // Whether the resolved track is a counter track.
    bool is_counter() const { return is_counter_; }

    // The UTID of the thread this track is associated with. Only valid when
    // |scope| == |Scope::kThread|.
    UniqueTid utid() const {
      PERFETTO_DCHECK(scope() == Scope::kThread);
      return utid_;
    }

    // The UPID of the process this track is associated with. Only valid when
    // |scope| == |Scope::kProcess|.
    UniquePid upid() const {
      PERFETTO_DCHECK(scope() == Scope::kProcess);
      return upid_;
    }

    // Whether this is a "root" track in its scope.
    // For example, a track for a given pid/tid is a root track but a track
    // which has a parent track is not.
    bool is_root() const { return is_root_; }

   private:
    friend class TrackEventTracker;

    ResolvedDescriptorTrack() = default;

    Scope scope_;
    bool is_counter_;
    bool is_root_;

    // Only set when |scope| == |Scope::kThread|.
    UniqueTid utid_;

    // Only set when |scope| == |Scope::kProcess|.
    UniquePid upid_;
  };

  explicit TrackEventTracker(TraceProcessorContext*);

  // Associate a TrackDescriptor track identified by the given |uuid| with a
  // given track description. This is called during tokenization. If a
  // reservation for the same |uuid| already exists, verifies that the present
  // reservation matches the new one.
  void ReserveDescriptorTrack(uint64_t uuid, const DescriptorTrackReservation&);

  // Resolves a descriptor track UUID to a `ResolvedDescriptorTrack` object.
  // This object contains information about the track's scope (global, process,
  // or thread) and other properties, but it does not create a track in the
  // `TrackTracker`. This should be called before `InternDescriptorTrack`.
  std::optional<ResolvedDescriptorTrack> ResolveDescriptorTrack(uint64_t uuid);

  // Interns a descriptor track for a "begin" slice event.
  //
  // This function will either return an existing track or create a new one
  // based on the track's UUID and other properties. For mergeable tracks, this
  // may involve using the |TrackCompressor| to find an appropriate track to
  // reuse.
  std::optional<TrackId> InternDescriptorTrackBegin(
      uint64_t uuid,
      StringId event_name,
      std::optional<uint32_t> packet_sequence_id) {
    State* s =
        EnsureDescriptorTrackInterned(uuid, event_name, packet_sequence_id);
    if (!s) {
      return std::nullopt;
    }
    if (std::holds_alternative<TrackId>(*s->track_id_or_factory)) {
      return base::unchecked_get<TrackId>(*s->track_id_or_factory);
    }
    const auto& factory = base::unchecked_get<TrackCompressor::TrackFactory>(
        *s->track_id_or_factory);
    return context_->track_compressor->Begin(factory,
                                             static_cast<int64_t>(uuid));
  }

  // Interns a descriptor track for an "end" slice event.
  //
  // See |InternDescriptorTrackBegin| for more details.
  std::optional<TrackId> InternDescriptorTrackEnd(
      uint64_t uuid,
      StringId event_name,
      std::optional<uint32_t> packet_sequence_id) {
    State* s =
        EnsureDescriptorTrackInterned(uuid, event_name, packet_sequence_id);
    if (!s) {
      return std::nullopt;
    }
    if (std::holds_alternative<TrackId>(*s->track_id_or_factory)) {
      return base::unchecked_get<TrackId>(*s->track_id_or_factory);
    }
    const auto& factory = base::unchecked_get<TrackCompressor::TrackFactory>(
        *s->track_id_or_factory);
    return context_->track_compressor->End(factory, static_cast<int64_t>(uuid));
  }

  // Interns a descriptor track for an "instant" slice event.
  //
  // See |InternDescriptorTrackBegin| for more details.
  std::optional<TrackId> InternDescriptorTrackInstant(
      uint64_t uuid,
      StringId event_name,
      std::optional<uint32_t> packet_sequence_id) {
    State* s =
        EnsureDescriptorTrackInterned(uuid, event_name, packet_sequence_id);
    if (!s) {
      return std::nullopt;
    }
    if (std::holds_alternative<TrackId>(*s->track_id_or_factory)) {
      return base::unchecked_get<TrackId>(*s->track_id_or_factory);
    }
    const auto& factory = base::unchecked_get<TrackCompressor::TrackFactory>(
        *s->track_id_or_factory);
    TrackId start =
        context_->track_compressor->Begin(factory, static_cast<int64_t>(uuid));
    TrackId end =
        context_->track_compressor->End(factory, static_cast<int64_t>(uuid));
    PERFETTO_DCHECK(start == end);
    return end;
  }

  // Interns a descriptor track for a counter event.
  //
  // This is similar to the other |InternDescriptorTrack*| functions but is
  // specifically for counters.
  std::optional<TrackId> InternDescriptorTrackCounter(
      uint64_t uuid,
      StringId event_name,
      std::optional<uint32_t> packet_sequence_id) {
    State* s =
        EnsureDescriptorTrackInterned(uuid, event_name, packet_sequence_id);
    if (!s) {
      return std::nullopt;
    }
    auto* track_id = std::get_if<TrackId>(&*s->track_id_or_factory);
    PERFETTO_CHECK(track_id);
    return *track_id;
  }

  // Interns a descriptor track for unspecified events.
  //
  // This is similar to the other |InternDescriptorTrack*| functions but is
  // specifically for unspecified events.
  std::optional<TrackId> InternDescriptorTrackLegacy(
      uint64_t uuid,
      StringId event_name,
      std::optional<uint32_t> packet_sequence_id) {
    State* s =
        EnsureDescriptorTrackInterned(uuid, event_name, packet_sequence_id);
    if (s && std::holds_alternative<TrackId>(*s->track_id_or_factory)) {
      return base::unchecked_get<TrackId>(*s->track_id_or_factory);
    }
    return std::nullopt;
  }

  // Converts the given counter value to an absolute value in the unit of the
  // counter, applying incremental delta encoding or unit multipliers as
  // necessary.
  std::optional<double> ConvertToAbsoluteCounterValue(
      PacketSequenceStateGeneration* packet_sequence_state,
      uint64_t counter_track_uuid,
      double value);

  void OnFirstPacketOnSequence(uint32_t packet_sequence_id);

  std::optional<int64_t> range_of_interest_start_us() const {
    return range_of_interest_start_us_;
  }

  void set_range_of_interest_us(int64_t range_of_interest_start_us) {
    range_of_interest_start_us_ = range_of_interest_start_us;
  }

 private:
  struct State {
    DescriptorTrackReservation reservation;
    std::optional<ResolvedDescriptorTrack> resolved = std::nullopt;
    std::optional<std::variant<TrackId, TrackCompressor::TrackFactory>>
        track_id_or_factory = std::nullopt;
  };

  std::optional<TrackId> InternDescriptorTrackForParent(
      uint64_t uuid,
      StringId event_name,
      std::optional<uint32_t> packet_sequence_id) {
    State* s =
        EnsureDescriptorTrackInterned(uuid, event_name, packet_sequence_id);
    if (!s) {
      return std::nullopt;
    }
    if (std::holds_alternative<TrackId>(*s->track_id_or_factory)) {
      return base::unchecked_get<TrackId>(*s->track_id_or_factory);
    }
    const auto& factory = base::unchecked_get<TrackCompressor::TrackFactory>(
        *s->track_id_or_factory);
    return context_->track_compressor->DefaultTrack(factory);
  }

  std::optional<TrackEventTracker::ResolvedDescriptorTrack>
  ResolveDescriptorTrackImpl(uint64_t uuid);

  State* EnsureDescriptorTrackInterned(
      uint64_t uuid,
      StringId event_name,
      std::optional<uint32_t> packet_sequence_id) {
    auto* s = descriptor_tracks_state_.Find(uuid);
    if (!s || !s->track_id_or_factory) {
      auto res =
          InternDescriptorTrackImpl(uuid, event_name, packet_sequence_id);
      if (!res) {
        return nullptr;
      }
      s = descriptor_tracks_state_.Find(uuid);
      PERFETTO_CHECK(s);
      s->track_id_or_factory = std::move(res);
    }
    return s;
  }

  std::optional<std::variant<TrackId, TrackCompressor::TrackFactory>>
  InternDescriptorTrackImpl(uint64_t uuid,
                            StringId event_name,
                            std::optional<uint32_t> packet_sequence_id);

  std::optional<std::variant<TrackId, TrackCompressor::TrackFactory>>
  CreateDescriptorTrack(uint64_t uuid,
                        StringId event_name,
                        std::optional<uint32_t> packet_sequence_id);

  bool IsTrackHierarchyValid(uint64_t uuid);

  void AddTrackArgs(uint64_t uuid,
                    std::optional<uint32_t> packet_sequence_id,
                    const DescriptorTrackReservation&,
                    bool,
                    ArgsTracker::BoundInserter&);

  // Helper to record analysis errors with track_uuid arg
  void RecordTrackError(size_t stat_key, uint64_t track_uuid);

  base::FlatHashMap<uint64_t /* uuid */, State> descriptor_tracks_state_;

  // Stores the descriptor uuid used for the primary process/thread track
  // for the given upid / utid. Used for pid/tid reuse detection.
  base::FlatHashMap<UniquePid, uint64_t /*uuid*/> descriptor_uuids_by_upid_;
  base::FlatHashMap<UniqueTid, uint64_t /*uuid*/> descriptor_uuids_by_utid_;

  std::unordered_set<uint32_t> sequences_with_first_packet_;

  const StringId source_key_;
  const StringId source_id_key_;
  const StringId is_root_in_scope_key_;
  const StringId category_key_;
  const StringId builtin_counter_type_key_;
  const StringId has_first_packet_on_sequence_key_id_;
  const StringId child_ordering_key_;
  const StringId explicit_id_;
  const StringId lexicographic_id_;
  const StringId chronological_id_;
  const StringId sibling_order_rank_key_;
  const StringId descriptor_source_;
  const StringId default_descriptor_track_name_;
  const StringId description_key_;
  const StringId y_axis_share_key_;
  const StringId track_uuid_key_id_;
  const StringId parent_uuid_key_id_;

  std::optional<int64_t> range_of_interest_start_us_;
  TraceProcessorContext* const context_;
};

}  // namespace perfetto::trace_processor

#endif  // SRC_TRACE_PROCESSOR_IMPORTERS_PROTO_TRACK_EVENT_TRACKER_H_
