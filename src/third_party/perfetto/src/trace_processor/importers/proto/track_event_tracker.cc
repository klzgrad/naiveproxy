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

#include "src/trace_processor/importers/proto/track_event_tracker.h"

#include <algorithm>
#include <array>
#include <cinttypes>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <unordered_set>
#include <utility>
#include <variant>

#include "perfetto/base/logging.h"
#include "src/trace_processor/importers/common/args_tracker.h"
#include "src/trace_processor/importers/common/import_logs_tracker.h"
#include "src/trace_processor/importers/common/process_track_translation_table.h"
#include "src/trace_processor/importers/common/process_tracker.h"
#include "src/trace_processor/importers/common/synthetic_tid.h"
#include "src/trace_processor/importers/common/track_compressor.h"
#include "src/trace_processor/importers/common/track_tracker.h"
#include "src/trace_processor/importers/common/tracks.h"
#include "src/trace_processor/importers/common/tracks_common.h"
#include "src/trace_processor/importers/common/tracks_internal.h"
#include "src/trace_processor/importers/proto/packet_sequence_state_generation.h"
#include "src/trace_processor/storage/stats.h"
#include "src/trace_processor/storage/trace_storage.h"
#include "src/trace_processor/types/trace_processor_context.h"
#include "src/trace_processor/types/variadic.h"

namespace perfetto::trace_processor {

namespace {

constexpr auto kThreadCounterTrackBlueprint = tracks::CounterBlueprint(
    "thread_counter_track_event",
    tracks::DynamicUnitBlueprint(),
    tracks::DimensionBlueprints(tracks::kThreadDimensionBlueprint,
                                tracks::LongDimensionBlueprint("track_uuid")),
    tracks::DynamicNameBlueprint());

constexpr auto kProcessCounterTrackBlueprint = tracks::CounterBlueprint(
    "process_counter_track_event",
    tracks::DynamicUnitBlueprint(),
    tracks::DimensionBlueprints(tracks::kProcessDimensionBlueprint,
                                tracks::LongDimensionBlueprint("track_uuid")),
    tracks::DynamicNameBlueprint());

constexpr auto kGlobalCounterTrackBlueprint = tracks::CounterBlueprint(
    "global_counter_track_event",
    tracks::DynamicUnitBlueprint(),
    tracks::DimensionBlueprints(tracks::LongDimensionBlueprint("track_uuid")),
    tracks::DynamicNameBlueprint());

constexpr auto kThreadTrackBlueprint = tracks::SliceBlueprint(
    "thread_track_event",
    tracks::DimensionBlueprints(tracks::kThreadDimensionBlueprint,
                                tracks::LongDimensionBlueprint("track_uuid")),
    tracks::DynamicNameBlueprint());

constexpr auto kProcessTrackBlueprint = tracks::SliceBlueprint(
    "process_track_event",
    tracks::DimensionBlueprints(tracks::kProcessDimensionBlueprint,
                                tracks::LongDimensionBlueprint("track_uuid")),
    tracks::DynamicNameBlueprint());

constexpr auto kGlobalTrackBlueprint = tracks::SliceBlueprint(
    "global_track_event",
    tracks::DimensionBlueprints(tracks::LongDimensionBlueprint("track_uuid")),
    tracks::DynamicNameBlueprint());

constexpr auto kThreadTrackMergedBlueprint = TrackCompressor::SliceBlueprint(
    "thread_merged_track_event",
    tracks::DimensionBlueprints(
        tracks::kThreadDimensionBlueprint,
        tracks::LongDimensionBlueprint("parent_track_uuid"),
        tracks::UintDimensionBlueprint("merge_key_type"),
        tracks::StringIdDimensionBlueprint("merge_key_value")),
    tracks::DynamicNameBlueprint());

constexpr auto kProcessTrackMergedBlueprint = TrackCompressor::SliceBlueprint(
    "process_merged_track_event",
    tracks::DimensionBlueprints(
        tracks::kProcessDimensionBlueprint,
        tracks::LongDimensionBlueprint("parent_track_uuid"),
        tracks::UintDimensionBlueprint("merge_key_type"),
        tracks::StringIdDimensionBlueprint("merge_key_value")),
    tracks::DynamicNameBlueprint());

constexpr auto kGlobalTrackMergedBlueprint = TrackCompressor::SliceBlueprint(
    "global_merged_track_event",
    tracks::DimensionBlueprints(
        tracks::LongDimensionBlueprint("parent_track_uuid"),
        tracks::UintDimensionBlueprint("merge_key_type"),
        tracks::StringIdDimensionBlueprint("merge_key_value")),
    tracks::DynamicNameBlueprint());

std::pair<uint32_t, StringId> GetMergeKey(
    const TrackEventTracker::DescriptorTrackReservation& reservation,
    StringId name) {
  using S = TrackEventTracker::DescriptorTrackReservation::SiblingMergeBehavior;
  switch (reservation.sibling_merge_behavior) {
    case S::kByKey:
      return std::make_pair(
          static_cast<uint32_t>(reservation.sibling_merge_behavior),
          reservation.sibling_merge_key);
    case S::kByName:
      return std::make_pair(
          static_cast<uint32_t>(reservation.sibling_merge_behavior), name);
    case S::kNone:
      PERFETTO_FATAL("Unreachable");
  }
  PERFETTO_FATAL("For GCC");
}

}  // namespace

TrackEventTracker::TrackEventTracker(TraceProcessorContext* context)
    : source_key_(context->storage->InternString("source")),
      source_id_key_(context->storage->InternString("trace_id")),
      is_root_in_scope_key_(context->storage->InternString("is_root_in_scope")),
      category_key_(context->storage->InternString("category")),
      builtin_counter_type_key_(
          context->storage->InternString("builtin_counter_type")),
      has_first_packet_on_sequence_key_id_(
          context->storage->InternString("has_first_packet_on_sequence")),
      child_ordering_key_(context->storage->InternString("child_ordering")),
      explicit_id_(context->storage->InternString("explicit")),
      lexicographic_id_(context->storage->InternString("lexicographic")),
      chronological_id_(context->storage->InternString("chronological")),
      sibling_order_rank_key_(
          context->storage->InternString("sibling_order_rank")),
      descriptor_source_(context->storage->InternString("descriptor")),
      default_descriptor_track_name_(
          context->storage->InternString("Default Track")),
      description_key_(context->storage->InternString("description")),
      y_axis_share_key_(context->storage->InternString("y_axis_share_key")),
      track_uuid_key_id_(context->storage->InternString("track_uuid")),
      parent_uuid_key_id_(context->storage->InternString("parent_uuid")),
      context_(context) {}

void TrackEventTracker::ReserveDescriptorTrack(
    uint64_t uuid,
    const DescriptorTrackReservation& reservation) {
  if (uuid == kDefaultDescriptorTrackUuid && reservation.parent_uuid) {
    context_->import_logs_tracker->RecordAnalysisError(
        stats::track_descriptor_default_track_with_parent,
        [&](ArgsTracker::BoundInserter& inserter) {
          inserter.AddArg(parent_uuid_key_id_,
                          Variadic::UnsignedInteger(reservation.parent_uuid));
        });
    return;
  }

  auto [it, inserted] = descriptor_tracks_state_.Insert(uuid, {reservation});
  if (inserted) {
    return;
  }
  if (!it->reservation.IsForSameTrack(reservation)) {
    RecordTrackError(stats::track_descriptor_conflicting_reservation, uuid);
    return;
  }

  if (!reservation.name.is_null()) {
    bool is_non_mergable_track =
        reservation.sibling_merge_behavior ==
        DescriptorTrackReservation::SiblingMergeBehavior::kNone;
    // If the previous value was null or this is a non-mergable track, update
    // the reservation name.
    if (it->reservation.name.is_null() || is_non_mergable_track) {
      it->reservation.name = reservation.name;
    }
    // Furthermore, if it's a non-mergable track, also update the name in the
    // track table if it exists.
    if (is_non_mergable_track && it->track_id_or_factory) {
      TrackId* track_id = std::get_if<TrackId>(&*it->track_id_or_factory);
      PERFETTO_CHECK(track_id);

      // If the track was already resolved, update the name.
      auto* tracks = context_->storage->mutable_track_table();
      auto rr = *tracks->FindById(*track_id);
      rr.set_name(reservation.name);
    }
  }
  it->reservation.min_timestamp =
      std::min(it->reservation.min_timestamp, reservation.min_timestamp);
}

std::optional<TrackEventTracker::ResolvedDescriptorTrack>
TrackEventTracker::ResolveDescriptorTrack(uint64_t uuid) {
  if (auto* ptr = descriptor_tracks_state_.Find(uuid); ptr && ptr->resolved) {
    return ptr->resolved;
  }
  auto resolved = ResolveDescriptorTrackImpl(uuid);
  auto* ptr = descriptor_tracks_state_.Find(uuid);
  PERFETTO_CHECK(ptr);
  ptr->resolved = std::move(resolved);
  return ptr->resolved;
}

std::optional<TrackEventTracker::ResolvedDescriptorTrack>
TrackEventTracker::ResolveDescriptorTrackImpl(uint64_t uuid) {
  State* state_ptr = descriptor_tracks_state_.Find(uuid);
  if (!state_ptr) {
    // If the track is not reserved, create a new reservation.
    // If the uuid is 0, it is the default descriptor track.
    // If the uuid is not 0, it is a user-defined descriptor track.
    DescriptorTrackReservation r;
    r.parent_uuid = 0;
    r.name = uuid == kDefaultDescriptorTrackUuid
                 ? default_descriptor_track_name_
                 : kNullStringId;
    ReserveDescriptorTrack(uuid, r);

    state_ptr = descriptor_tracks_state_.Find(uuid);
    PERFETTO_CHECK(state_ptr);
  }

  // Before trying to resolve anything, ensure that the hierarchy of tracks is
  // well defined.
  if (!IsTrackHierarchyValid(uuid)) {
    return std::nullopt;
  }

  // Resolve process and thread id for tracks produced from within a pid
  // namespace.
  //
  // Get the root-level trusted_pid for the process that produces the track
  // event.
  std::optional<uint32_t> trusted_pid =
      context_->process_tracker->GetTrustedPid(uuid);
  DescriptorTrackReservation& reservation = state_ptr->reservation;

  // Try to resolve to root-level pid and tid if the process is pid-namespaced.
  if (trusted_pid && reservation.pid) {
    std::optional<uint32_t> resolved_pid =
        context_->process_tracker->ResolveNamespacedTid(*trusted_pid,
                                                        *reservation.pid);
    if (resolved_pid) {
      reservation.pid = resolved_pid;
    }
  }
  std::optional<uint32_t> resolved_tid;
  if (trusted_pid && reservation.tid) {
    resolved_tid = context_->process_tracker->ResolveNamespacedTid(
        *trusted_pid, *reservation.tid);
  }
  if (resolved_tid) {
    reservation.tid = resolved_tid;
  } else if (reservation.use_synthetic_tid && reservation.tid &&
             reservation.pid) {
    reservation.tid = CreateSyntheticTid(*reservation.tid, *reservation.pid);
  }

  // Try to resolve any parent tracks recursively, too.
  std::optional<ResolvedDescriptorTrack> parent_resolved_track;
  if (reservation.parent_uuid != kDefaultDescriptorTrackUuid) {
    parent_resolved_track = ResolveDescriptorTrack(reservation.parent_uuid);
  }

  if (reservation.tid) {
    UniqueTid utid = context_->process_tracker->UpdateThread(*reservation.tid,
                                                             *reservation.pid);
    auto [it, inserted] = descriptor_uuids_by_utid_.Insert(utid, uuid);
    if (!inserted) {
      // We already saw a another track with a different uuid for this thread.
      // Since there should only be one descriptor track for each thread, we
      // assume that its tid was reused. So, start a new thread.
      uint64_t old_uuid = *it;
      PERFETTO_DCHECK(old_uuid != uuid);  // Every track is only resolved once.
      *it = uuid;

      PERFETTO_DLOG("Detected tid reuse (pid: %" PRId64 " tid: %" PRId64
                    ") from track descriptors (old uuid: %" PRIu64
                    " new uuid: %" PRIu64 " timestamp: %" PRId64 ")",
                    *reservation.pid, *reservation.tid, old_uuid, uuid,
                    reservation.min_timestamp);

      // Associate the new thread with its process.
      utid = context_->process_tracker->StartNewThread(std::nullopt,
                                                       *reservation.tid);
      UniqueTid updated_utid = context_->process_tracker->UpdateThread(
          *reservation.tid, *reservation.pid);
      PERFETTO_CHECK(updated_utid == utid);
    }
    return ResolvedDescriptorTrack::Thread(utid, reservation.is_counter, true);
  }

  if (reservation.pid) {
    UniquePid upid =
        context_->process_tracker->GetOrCreateProcess(*reservation.pid);
    auto [it, inserted] = descriptor_uuids_by_upid_.Insert(upid, uuid);
    if (!inserted) {
      // We already saw a another track with a different uuid for this process.
      // Since there should only be one descriptor track for each process,
      // we assume that its pid was reused. So, start a new process.
      uint64_t old_uuid = *it;
      PERFETTO_DCHECK(old_uuid != uuid);  // Every track is only resolved once.
      *it = uuid;

      PERFETTO_DLOG("Detected pid reuse (pid: %" PRId64
                    ") from track descriptors (old uuid: %" PRIu64
                    " new uuid: %" PRIu64 " timestamp: %" PRId64 ")",
                    *reservation.pid, old_uuid, uuid,
                    reservation.min_timestamp);

      upid = context_->process_tracker->StartNewProcess(
          std::nullopt, std::nullopt, *reservation.pid, kNullStringId,
          ThreadNamePriority::kTrackDescriptor);
    }
    return ResolvedDescriptorTrack::Process(upid, reservation.is_counter, true);
  }

  if (parent_resolved_track) {
    switch (parent_resolved_track->scope()) {
      case ResolvedDescriptorTrack::Scope::kThread:
        return ResolvedDescriptorTrack::Thread(parent_resolved_track->utid(),
                                               reservation.is_counter,
                                               false /* is_root */);
      case ResolvedDescriptorTrack::Scope::kProcess:
        return ResolvedDescriptorTrack::Process(parent_resolved_track->upid(),
                                                reservation.is_counter,
                                                false /* is_root*/);
      case ResolvedDescriptorTrack::Scope::kGlobal:
        break;
    }
  }
  return ResolvedDescriptorTrack::Global(reservation.is_counter);
}

std::optional<std::variant<TrackId, TrackCompressor::TrackFactory>>
TrackEventTracker::InternDescriptorTrackImpl(
    uint64_t uuid,
    StringId event_name,
    std::optional<uint32_t> packet_sequence_id) {
  std::optional<ResolvedDescriptorTrack> resolved =
      ResolveDescriptorTrack(uuid);
  if (!resolved) {
    return std::nullopt;
  }
  State* state = descriptor_tracks_state_.Find(uuid);
  PERFETTO_CHECK(state);

  DescriptorTrackReservation* reservation = &state->reservation;

  // Try to resolve any parent tracks recursively, too.
  std::optional<TrackId> parent_track_id;
  std::optional<ResolvedDescriptorTrack> parent_resolved_track;
  if (reservation->parent_uuid != kDefaultDescriptorTrackUuid) {
    parent_track_id = InternDescriptorTrackForParent(
        reservation->parent_uuid, kNullStringId, packet_sequence_id);
    parent_resolved_track = ResolveDescriptorTrack(reservation->parent_uuid);
  }

  // Don't capture anything by reference in these functions as they are
  // persisted in the case of merged tracks.
  TrackTracker::SetArgsCallback args_fn_root =
      [this, uuid, packet_sequence_id](ArgsTracker::BoundInserter& inserter) {
        State* state = descriptor_tracks_state_.Find(uuid);
        PERFETTO_CHECK(state);
        AddTrackArgs(uuid, packet_sequence_id, state->reservation,
                     true /* is_root*/, inserter);
      };
  TrackTracker::SetArgsCallback args_fn_non_root =
      [this, uuid, packet_sequence_id](ArgsTracker::BoundInserter& inserter) {
        State* state = descriptor_tracks_state_.Find(uuid);
        PERFETTO_CHECK(state);
        AddTrackArgs(uuid, packet_sequence_id, state->reservation,
                     false /* is_root*/, inserter);
      };
  if (resolved->is_root()) {
    switch (resolved->scope()) {
      case ResolvedDescriptorTrack::Scope::kThread:
        if (resolved->is_counter()) {
          return context_->track_tracker->InternTrack(
              kThreadCounterTrackBlueprint,
              tracks::Dimensions(resolved->utid(), static_cast<int64_t>(uuid)),
              tracks::DynamicName(reservation->name), args_fn_root,
              tracks::DynamicUnit(reservation->counter_details->unit));
        } else if (reservation->use_separate_track) {
          return context_->track_tracker->InternTrack(
              kThreadTrackBlueprint,
              tracks::Dimensions(resolved->utid(), static_cast<int64_t>(uuid)),
              tracks::DynamicName(reservation->name), args_fn_root);
        }
        return context_->track_tracker->InternThreadTrack(resolved->utid());
      case ResolvedDescriptorTrack::Scope::kProcess: {
        StringId translated_name =
            context_->process_track_translation_table->TranslateName(
                reservation->name);
        if (reservation->is_counter) {
          return context_->track_tracker->InternTrack(
              kProcessCounterTrackBlueprint,
              tracks::Dimensions(resolved->upid(), static_cast<int64_t>(uuid)),
              tracks::DynamicName(translated_name), args_fn_root,
              tracks::DynamicUnit(reservation->counter_details->unit));
        }
        return context_->track_tracker->InternTrack(
            kProcessTrackBlueprint,
            tracks::Dimensions(resolved->upid(), static_cast<int64_t>(uuid)),
            tracks::DynamicName(translated_name), args_fn_root);
      }
      case ResolvedDescriptorTrack::Scope::kGlobal:
        PERFETTO_FATAL("Should never happen");
    }
  }

  StringId name = reservation->name.is_null() ? event_name : reservation->name;
  // Don't capture anything by reference in these functions as they are
  // persisted in the case of merged tracks.
  auto set_parent_id = [this, parent_track_id](TrackId id) {
    if (parent_track_id) {
      auto rr = context_->storage->mutable_track_table()->FindById(id);
      PERFETTO_CHECK(rr);
      rr->set_parent_id(parent_track_id);
    }
  };
  using M = TrackEventTracker::DescriptorTrackReservation::SiblingMergeBehavior;
  if (parent_track_id) {
    // If we have the track id, we should also always have the resolved track
    // too.
    PERFETTO_CHECK(parent_resolved_track);
    switch (parent_resolved_track->scope()) {
      case ResolvedDescriptorTrack::Scope::kThread: {
        // If parent is a thread track, create another thread-associated track.
        if (reservation->is_counter) {
          TrackId id = context_->track_tracker->InternTrack(
              kThreadCounterTrackBlueprint,
              tracks::Dimensions(parent_resolved_track->utid(),
                                 static_cast<int64_t>(uuid)),
              tracks::DynamicName(reservation->name), args_fn_non_root,
              tracks::DynamicUnit(reservation->counter_details->unit));
          // If the parent has a process descriptor set, promote this track
          // to also be a root thread level track. This is necessary for
          // backcompat reasons: see the comment on parent_uuid in
          // TrackDescriptor.
          if (!parent_resolved_track->is_root()) {
            set_parent_id(id);
          }
          return id;
        }
        if (reservation->sibling_merge_behavior == M::kNone) {
          TrackId id = context_->track_tracker->InternTrack(
              kThreadTrackBlueprint,
              tracks::Dimensions(parent_resolved_track->utid(),
                                 static_cast<int64_t>(uuid)),
              tracks::DynamicName(name), args_fn_non_root);
          // If the parent has a process descriptor set, promote this track
          // to also be a root thread level track. This is necessary for
          // backcompat reasons: see the comment on parent_uuid in
          // TrackDescriptor.
          if (!parent_resolved_track->is_root()) {
            set_parent_id(id);
          }
          return id;
        }
        auto [type, key] = GetMergeKey(*reservation, name);
        return context_->track_compressor->CreateTrackFactory(
            kThreadTrackMergedBlueprint,
            tracks::Dimensions(parent_resolved_track->utid(),
                               static_cast<int64_t>(reservation->parent_uuid),
                               type, key),
            tracks::DynamicName(name), args_fn_non_root,
            parent_resolved_track->is_root() ? std::function<void(TrackId)>()
                                             : set_parent_id);
      }
      case ResolvedDescriptorTrack::Scope::kProcess: {
        // If parent is a process track, create another process-associated
        // track.
        if (reservation->is_counter) {
          StringId translated_name =
              context_->process_track_translation_table->TranslateName(
                  reservation->name);
          TrackId id = context_->track_tracker->InternTrack(
              kProcessCounterTrackBlueprint,
              tracks::Dimensions(parent_resolved_track->upid(),
                                 static_cast<int64_t>(uuid)),
              tracks::DynamicName(translated_name), args_fn_non_root,
              tracks::DynamicUnit(reservation->counter_details->unit));
          // If the parent has a thread descriptor set, promote this track
          // to also be a root thread level track. This is necessary for
          // backcompat reasons: see the comment on parent_uuid in
          // TrackDescriptor.
          if (!parent_resolved_track->is_root()) {
            set_parent_id(id);
          }
          return id;
        }
        StringId translated_name =
            context_->process_track_translation_table->TranslateName(name);
        if (reservation->sibling_merge_behavior == M::kNone) {
          TrackId id = context_->track_tracker->InternTrack(
              kProcessTrackBlueprint,
              tracks::Dimensions(parent_resolved_track->upid(),
                                 static_cast<int64_t>(uuid)),
              tracks::DynamicName(translated_name), args_fn_non_root);
          // If the parent has a thread descriptor set, promote this track
          // to also be a root thread level track. This is necessary for
          // backcompat reasons: see the comment on parent_uuid in
          // TrackDescriptor.
          if (!parent_resolved_track->is_root()) {
            set_parent_id(id);
          }
          return id;
        }
        auto [type, key] = GetMergeKey(*reservation, translated_name);
        return context_->track_compressor->CreateTrackFactory(
            kProcessTrackMergedBlueprint,
            tracks::Dimensions(parent_resolved_track->upid(),
                               static_cast<int64_t>(reservation->parent_uuid),
                               type, key),
            tracks::DynamicName(translated_name), args_fn_non_root,
            parent_resolved_track->is_root() ? std::function<void(TrackId)>()
                                             : set_parent_id);
      }
      case ResolvedDescriptorTrack::Scope::kGlobal:
        break;
    }
  }

  // root_in_scope only matters for legacy JSON export. This is somewhat related
  // but intentionally distinct from our handling of parent_id relationships.
  bool is_root_in_scope = uuid == kDefaultDescriptorTrackUuid;
  if (reservation->is_counter) {
    TrackId id = context_->track_tracker->InternTrack(
        kGlobalCounterTrackBlueprint,
        tracks::Dimensions(static_cast<int64_t>(uuid)),
        tracks::DynamicName(reservation->name),
        is_root_in_scope ? args_fn_root : args_fn_non_root,
        tracks::DynamicUnit(reservation->counter_details->unit));
    set_parent_id(id);
    return id;
  }
  if (reservation->sibling_merge_behavior == M::kNone) {
    TrackId id = context_->track_tracker->InternTrack(
        kGlobalTrackBlueprint, tracks::Dimensions(static_cast<int64_t>(uuid)),
        tracks::DynamicName(name),
        is_root_in_scope ? args_fn_root : args_fn_non_root);
    set_parent_id(id);
    return id;
  }
  auto [type, key] = GetMergeKey(*reservation, name);
  return context_->track_compressor->CreateTrackFactory(
      kGlobalTrackMergedBlueprint,
      tracks::Dimensions(static_cast<int64_t>(reservation->parent_uuid), type,
                         key),
      tracks::DynamicName(name),
      is_root_in_scope ? args_fn_root : args_fn_non_root, set_parent_id);
}

std::optional<double> TrackEventTracker::ConvertToAbsoluteCounterValue(
    PacketSequenceStateGeneration* packet_sequence_state,
    uint64_t counter_track_uuid,
    double value) {
  auto* state_ptr = descriptor_tracks_state_.Find(counter_track_uuid);
  if (!state_ptr) {
    PERFETTO_DLOG("Unknown counter track with uuid %" PRIu64,
                  counter_track_uuid);
    return std::nullopt;
  }

  DescriptorTrackReservation& reservation = state_ptr->reservation;
  if (!reservation.is_counter) {
    PERFETTO_DLOG("Track with uuid %" PRIu64 " is not a counter track",
                  counter_track_uuid);
    return std::nullopt;
  }
  if (!reservation.counter_details) {
    PERFETTO_FATAL("Counter tracks require `counter_details`.");
  }

  DescriptorTrackReservation::CounterDetails& c_details =
      *reservation.counter_details;
  if (c_details.unit_multiplier > 0) {
    value *= static_cast<double>(c_details.unit_multiplier);
  }
  if (c_details.is_incremental) {
    value = packet_sequence_state->IncrementAndGetCounterValue(
        counter_track_uuid, value);
  }
  return value;
}

void TrackEventTracker::OnFirstPacketOnSequence(uint32_t packet_sequence_id) {
  sequences_with_first_packet_.insert(packet_sequence_id);
}

void TrackEventTracker::AddTrackArgs(
    uint64_t uuid,
    std::optional<uint32_t> packet_sequence_id,
    const DescriptorTrackReservation& reservation,
    bool is_root_in_scope,
    ArgsTracker::BoundInserter& args) {
  args.AddArg(source_key_, Variadic::String(descriptor_source_))
      .AddArg(source_id_key_, Variadic::Integer(static_cast<int64_t>(uuid)))
      .AddArg(is_root_in_scope_key_, Variadic::Boolean(is_root_in_scope));
  if (reservation.counter_details) {
    if (!reservation.counter_details->category.is_null()) {
      args.AddArg(category_key_,
                  Variadic::String(reservation.counter_details->category));
    }
    if (!reservation.counter_details->builtin_type_str.is_null()) {
      args.AddArg(
          builtin_counter_type_key_,
          Variadic::String(reservation.counter_details->builtin_type_str));
    }
    if (!reservation.counter_details->y_axis_share_key.is_null()) {
      args.AddArg(
          y_axis_share_key_,
          Variadic::String(reservation.counter_details->y_axis_share_key));
    }
  }
  if (packet_sequence_id &&
      sequences_with_first_packet_.find(*packet_sequence_id) !=
          sequences_with_first_packet_.end()) {
    args.AddArg(has_first_packet_on_sequence_key_id_, Variadic::Boolean(true));
  }

  switch (reservation.ordering) {
    case DescriptorTrackReservation::ChildTracksOrdering::kLexicographic:
      args.AddArg(child_ordering_key_, Variadic::String(lexicographic_id_));
      break;
    case DescriptorTrackReservation::ChildTracksOrdering::kChronological:
      args.AddArg(child_ordering_key_, Variadic::String(chronological_id_));
      break;
    case DescriptorTrackReservation::ChildTracksOrdering::kExplicit:
      args.AddArg(child_ordering_key_, Variadic::String(explicit_id_));
      break;
    case DescriptorTrackReservation::ChildTracksOrdering::kUnknown:
      break;
  }

  if (reservation.sibling_order_rank) {
    args.AddArg(sibling_order_rank_key_,
                Variadic::Integer(*reservation.sibling_order_rank));
  }

  if (!reservation.description.is_null()) {
    args.AddArg(description_key_, Variadic::String(reservation.description));
  }
}

bool TrackEventTracker::IsTrackHierarchyValid(uint64_t uuid) {
  // Do a basic tree walking algorithm to find if there are duplicate ids or
  // the path to the root is longer than kMaxAncestors.
  static constexpr size_t kMaxAncestors = 100;
  std::array<uint64_t, kMaxAncestors> seen;
  uint64_t current_uuid = uuid;
  for (uint32_t i = 0; i < kMaxAncestors; ++i) {
    if (current_uuid == 0) {
      return true;
    }
    for (uint32_t j = 0; j < i; ++j) {
      if (current_uuid == seen[j]) {
        RecordTrackError(stats::track_event_track_hierarchy_loop, uuid);
        return false;
      }
    }
    auto* state_ptr = descriptor_tracks_state_.Find(current_uuid);
    if (!state_ptr) {
      RecordTrackError(stats::track_hierarchy_missing_uuid, uuid);
      return false;
    }
    seen[i] = current_uuid;
    current_uuid = state_ptr->reservation.parent_uuid;
  }
  RecordTrackError(stats::track_event_track_hierarchy_too_deep, uuid);
  return false;
}

void TrackEventTracker::RecordTrackError(size_t stat_key, uint64_t track_uuid) {
  context_->import_logs_tracker->RecordAnalysisError(
      stat_key, [this, track_uuid](ArgsTracker::BoundInserter& inserter) {
        inserter.AddArg(track_uuid_key_id_,
                        Variadic::UnsignedInteger(track_uuid));
      });
}

TrackEventTracker::ResolvedDescriptorTrack
TrackEventTracker::ResolvedDescriptorTrack::Process(UniquePid upid,
                                                    bool is_counter,
                                                    bool is_root) {
  ResolvedDescriptorTrack track;
  track.scope_ = Scope::kProcess;
  track.is_counter_ = is_counter;
  track.upid_ = upid;
  track.is_root_ = is_root;
  return track;
}

TrackEventTracker::ResolvedDescriptorTrack
TrackEventTracker::ResolvedDescriptorTrack::Thread(UniqueTid utid,
                                                   bool is_counter,
                                                   bool is_root) {
  ResolvedDescriptorTrack track;
  track.scope_ = Scope::kThread;
  track.is_counter_ = is_counter;
  track.utid_ = utid;
  track.is_root_ = is_root;
  return track;
}

TrackEventTracker::ResolvedDescriptorTrack
TrackEventTracker::ResolvedDescriptorTrack::Global(bool is_counter) {
  ResolvedDescriptorTrack track;
  track.scope_ = Scope::kGlobal;
  track.is_counter_ = is_counter;
  track.is_root_ = false;
  return track;
}

}  // namespace perfetto::trace_processor
