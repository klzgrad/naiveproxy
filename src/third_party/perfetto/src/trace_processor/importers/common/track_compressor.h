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

#ifndef SRC_TRACE_PROCESSOR_IMPORTERS_COMMON_TRACK_COMPRESSOR_H_
#define SRC_TRACE_PROCESSOR_IMPORTERS_COMMON_TRACK_COMPRESSOR_H_

#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <string_view>
#include <tuple>
#include <type_traits>
#include <utility>
#include <vector>

#include "perfetto/base/logging.h"
#include "perfetto/ext/base/flat_hash_map.h"
#include "perfetto/ext/base/hash.h"
#include "perfetto/public/compiler.h"
#include "src/trace_processor/importers/common/track_tracker.h"
#include "src/trace_processor/importers/common/tracks.h"
#include "src/trace_processor/importers/common/tracks_internal.h"
#include "src/trace_processor/storage/trace_storage.h"
#include "src/trace_processor/types/trace_processor_context.h"

namespace perfetto::trace_processor {

class TrackCompressorUnittest;

namespace internal {

template <typename Ds, size_t r, size_t... Is>
constexpr auto UncompressedDimensions(Ds,
                                      std::integral_constant<size_t, r>,
                                      std::index_sequence<Is...>) {
  static_assert(r > 0,
                "Wrong blueprint passed to TrackCompressor Intern* function. "
                "Make sure Blueprint was created using "
                "TrackCompressor::SliceBlueprint *not* tracks::SliceBlueprint");
  return tracks::Dimensions(std::tuple_element_t<Is, Ds>()...);
}

template <typename BlueprintT>
using uncompressed_dimensions_t = decltype(UncompressedDimensions(
    typename BlueprintT::dimensions_t(),
    std::integral_constant<
        size_t,
        std::tuple_size_v<typename BlueprintT::dimensions_t>>(),
    std::make_index_sequence<
        std::tuple_size_v<typename BlueprintT::dimensions_t> == 0
            ? 0
            : std::tuple_size_v<typename BlueprintT::dimensions_t> - 1>()));

}  // namespace internal

// Keeps track of the track group count across multiple traces/machines to
// avoid clashes.
struct TrackCompressorGroupIdxState {
  uint32_t track_groups = 0;
};

// "Compresses" and interns trace processor tracks for a given track type.
//
// When writing traces, sometimes it's not possible to reuse tracks meaning
// people create one track per event. Creating a new track for every event,
// however, leads to an explosion of tracks which is undesirable. This class
// exists to multiplex slices so that multiple events correspond to a single
// track in a way which minimises the number of tracks.
class TrackCompressor {
 private:
  struct TrackSet;

 public:
  // Indicates the nesting behaviour of slices associated to a single slice
  // stack.
  enum class NestingBehaviour {
    // Indicates that slices are nestable; that is, a stack of slices with
    // the same cookie should stack properly, not merely overlap.
    //
    // This pattern should be the default behaviour that most async slices
    // should use.
    kNestable,

    // Indicates that slices are unnestable but also saturating; that is
    // calling Begin -> Begin only causes a single Begin to be recorded.
    // This is only really useful for Android async slices which have this
    // behaviour for legacy reasons. See the comment in
    // SystraceParser::ParseSystracePoint for information on why
    // this behaviour exists.
    kLegacySaturatingUnnestable,
  };

  // Contains all the information about a set of tracks which can be merged
  // together. This is essentially a factory for tracks which will be created
  // on-demand.
  struct TrackFactory {
    uint64_t hash;
    NestingBehaviour behaviour;
    std::function<TrackId(const TrackSet&, uint32_t index)> factory;
  };

  explicit TrackCompressor(TraceProcessorContext* context);
  ~TrackCompressor() = default;

  /****************************************************************************
   *                 RECOMMENDED API FOR MOST USE CASES
   ****************************************************************************/

  // Starts a new slice which has the given cookie.
  template <typename BlueprintT>
  TrackId InternBegin(
      const BlueprintT& bp,
      const internal::uncompressed_dimensions_t<BlueprintT>& dims,
      int64_t cookie,
      const typename BlueprintT::name_t& name = tracks::BlueprintName(),
      TrackTracker::SetArgsCallback args = {}) {
    return Begin(CreateTrackFactory(bp, dims, name, args), cookie);
  }

  // Ends a new slice which has the given cookie.
  template <typename BlueprintT>
  TrackId InternEnd(
      BlueprintT bp,
      const internal::uncompressed_dimensions_t<BlueprintT>& dims,
      int64_t cookie,
      const typename BlueprintT::name_t& name = tracks::BlueprintName(),
      TrackTracker::SetArgsCallback args = {}) {
    return End(CreateTrackFactory(bp, dims, name, args), cookie);
  }

  // Creates a scoped slice.
  // This method makes sure that any other slice in this track set does
  // not happen simultaneously on the returned track.
  template <typename BlueprintT>
  TrackId InternScoped(
      BlueprintT bp,
      const internal::uncompressed_dimensions_t<BlueprintT>& dims,
      int64_t ts,
      int64_t dur,
      const typename BlueprintT::name_t& name = tracks::BlueprintName(),
      TrackTracker::SetArgsCallback args = {}) {
    return Scoped(CreateTrackFactory(bp, dims, name, args), ts, dur);
  }

  // Wrapper function for `InternTrack` for legacy "async" style tracks which
  // is supported by the Chrome JSON format and other derivative formats
  // (e.g. Fuchsia).
  //
  // WARNING: this function should *not* be used by any users not explicitly
  // approved and discussed with a trace processor maintainer.
  enum class AsyncSliceType { kBegin, kEnd, kInstant };
  TrackId InternLegacyAsyncTrack(StringId name,
                                 uint32_t upid,
                                 int64_t trace_id,
                                 bool trace_id_is_process_scoped,
                                 StringId source_scope,
                                 AsyncSliceType slice_type);

  // Wrapper around tracks::SliceBlueprint which makes the blueprint eligible
  // for compression with TrackCompressor. Please see documentation of
  // tracks::SliceBlueprint for usage.
  template <typename NB = tracks::NameBlueprintT::Auto, typename... D>
  static constexpr auto SliceBlueprint(
      const char type[],
      tracks::DimensionBlueprintsT<D...> dimensions = {},
      NB name = NB{}) {
    auto blueprint = tracks::SliceBlueprint(type, dimensions, name);
    using BT = decltype(blueprint);
    constexpr auto kCompressorIdxDimensionIndex =
        std::tuple_size_v<typename BT::dimension_blueprints_t>;
    return std::apply(
        [&](auto... x) {
          auto blueprints = blueprint.dimension_blueprints;
          blueprints[kCompressorIdxDimensionIndex] =
              tracks::UintDimensionBlueprint("track_compressor_idx");

          if constexpr (std::is_base_of_v<tracks::NameBlueprintT::FnBase,
                                          typename BT::name_blueprint_t>) {
            using F = decltype(blueprint.name_blueprint.fn);
            auto fn =
                MakeNameFn<F, decltype(x)...>(blueprint.name_blueprint.fn);
            return tracks::BlueprintT<
                decltype(fn), typename BT::unit_blueprint_t,
                typename BT::description_blueprint_t, decltype(x)...,
                tracks::DimensionBlueprintT<uint32_t>>{
                {
                    blueprint.event_type,
                    blueprint.type,
                    blueprint.hasher,
                    blueprints,
                },
                fn,
                blueprint.unit_blueprint,
                blueprint.description_blueprint,
            };
          } else {
            return tracks::BlueprintT<
                typename BT::name_blueprint_t, typename BT::unit_blueprint_t,
                typename BT::description_blueprint_t, decltype(x)...,
                tracks::DimensionBlueprintT<uint32_t>>{
                {
                    blueprint.event_type,
                    blueprint.type,
                    blueprint.hasher,
                    blueprints,
                },
                blueprint.name_blueprint,
                blueprint.unit_blueprint,
                blueprint.description_blueprint,
            };
          }
        },
        typename BT::dimension_blueprints_t());
  }

  /***************************************************************************
   *         ADVANCED API FOR PERFORMANCE-CRITICAL CODE PATHS
   ***************************************************************************/

  // Computes a hash of the given blueprint and dimensions which can be used
  // in the functions below.
  // This function is intended to be used on hot paths where the hash can be
  // cached and reused across multiple calls.
  template <typename BlueprintT>
  TrackFactory CreateTrackFactory(
      const BlueprintT& bp,
      const internal::uncompressed_dimensions_t<BlueprintT>& dims,
      const typename BlueprintT::name_t& name = tracks::BlueprintName(),
      TrackTracker::SetArgsCallback args = {},
      std::function<void(TrackId)> on_new_track = {}) {
    return TrackFactory{
        tracks::HashFromBlueprintAndDimensions(bp, dims),
        TypeToNestingBehaviour(bp.type),
        [this, dims, bp, name, args = std::move(args),
         on_new_track = std::move(on_new_track)](const TrackSet& state,
                                                 uint32_t idx) {
          auto final_dims = std::tuple_cat(dims, std::make_tuple(idx));
          TrackId track_id =
              context_->track_tracker->CreateTrack(bp, final_dims, name, args);
          if (on_new_track) {
            on_new_track(track_id);
          }
          auto rr =
              context_->storage->mutable_track_table()->FindById(track_id);
          rr->set_track_group_id(state.set_id);
          return track_id;
        },
    };
  }

  // Starts a new slice which has the given cookie.
  //
  // This is an advanced version of |InternBegin| which should only be used
  // on hot paths where the |hash| is cached. For most usecases, |InternBegin|
  // should be preferred.
  PERFETTO_ALWAYS_INLINE TrackId Begin(const TrackFactory& factory,
                                       int64_t cookie) {
    TrackSet& set = GetOrCreateTrackSet(factory.hash);
    auto [track_id_ptr, idx] = BeginInternal(set, factory.behaviour, cookie);
    if (*track_id_ptr == kInvalidTrackId) {
      *track_id_ptr = factory.factory(set, idx);
    }
    return *track_id_ptr;
  }

  // Ends a new slice which has the given cookie.
  //
  // This is an advanced version of |InternEnd| which should only be used
  // on hot paths where the |hash| is cached. For most usecases, |InternEnd|
  // should be preferred.
  PERFETTO_ALWAYS_INLINE TrackId End(const TrackFactory& factory,
                                     int64_t cookie) {
    TrackSet& set = GetOrCreateTrackSet(factory.hash);
    auto [track_id_ptr, idx] = EndInternal(set, cookie);
    if (*track_id_ptr == kInvalidTrackId) {
      *track_id_ptr = factory.factory(set, idx);
    }
    return *track_id_ptr;
  }

  // Creates a scoped slice.
  //
  // This is an advanced version of |InternScoped| which should only be used
  // on hot paths where the |hash| is cached. For most usecases, |InternScoped|
  // should be preferred.
  PERFETTO_ALWAYS_INLINE TrackId Scoped(const TrackFactory& factory,
                                        int64_t ts,
                                        int64_t dur) {
    TrackSet& set = GetOrCreateTrackSet(factory.hash);
    auto [track_id_ptr, idx] = ScopedInternal(set, ts, dur);
    if (*track_id_ptr == kInvalidTrackId) {
      *track_id_ptr = factory.factory(set, idx);
    }
    return *track_id_ptr;
  }

  // Returns the track with index 0 for the given factory, creating it if it
  // doesn't exist.
  //
  // This is useful for cases where a "default" track is needed for a given
  // factory. For example, if we need the "representative" track to act as a
  // parent for a merged group of tracks.
  PERFETTO_ALWAYS_INLINE TrackId DefaultTrack(const TrackFactory& factory) {
    TrackSet& set = GetOrCreateTrackSet(factory.hash);
    if (set.tracks.empty()) {
      uint32_t idx = GetOrCreateTrackForCookie(set.tracks, 0);
      PERFETTO_DCHECK(idx == 0);
      set.tracks.front().track_id = factory.factory(set, idx);
    }
    return set.tracks.front().track_id;
  }

 private:
  friend class TrackCompressorUnittest;

  struct TrackState {
    enum class SliceType { kCookie, kTimestamp };
    SliceType slice_type;

    union {
      // Only valid for |slice_type| == |SliceType::kCookie|.
      int64_t cookie;

      // Only valid for |slice_type| == |SliceType::kTimestamp|.
      int64_t ts_end;
    };

    // Only used for |slice_type| == |SliceType::kCookie|.
    uint32_t nest_count;

    // The track id for this state. This is cached because it is expensive to
    // compute.
    TrackId track_id = kInvalidTrackId;
  };

  struct TrackSet {
    uint32_t set_id;
    std::vector<TrackState> tracks;
  };

  std::pair<TrackId*, uint32_t> BeginInternal(TrackSet&,
                                              NestingBehaviour,
                                              int64_t cookie);

  std::pair<TrackId*, uint32_t> EndInternal(TrackSet&, int64_t cookie);

  std::pair<TrackId*, uint32_t> ScopedInternal(TrackSet&,
                                               int64_t ts,
                                               int64_t dur);

  static constexpr NestingBehaviour TypeToNestingBehaviour(
      std::string_view type) {
    if (type == "atrace_async_slice") {
      return NestingBehaviour::kLegacySaturatingUnnestable;
    }
    return NestingBehaviour::kNestable;
  }

  template <typename F, typename... T>
  static constexpr auto MakeNameFn(F fn) {
    auto f = [fn](typename T::type... y, uint32_t) { return fn(y...); };
    return tracks::NameBlueprintT::Fn<decltype(f)>{{}, f};
  }

  // Returns the state for a track using the following algorithm:
  // 1. If a track exists with the given cookie in the vector, returns
  //    that track.
  // 2. Otherwise, looks for any track in the set which is "open" (i.e.
  //    does not have another slice currently scheduled).
  // 3. Otherwise, creates a new track and adds it to the vector.
  static uint32_t GetOrCreateTrackForCookie(std::vector<TrackState>& tracks,
                                            int64_t cookie);

  PERFETTO_ALWAYS_INLINE TrackSet& GetOrCreateTrackSet(uint64_t hash) {
    auto [it, inserted] = sets_.Insert(hash, {});
    if (inserted) {
      it->set_id = context_->track_group_idx_state->track_groups++;
    }
    return *it;
  }

  base::FlatHashMap<uint64_t, TrackSet, base::AlreadyHashed<uint64_t>> sets_;
  base::FlatHashMap<uint64_t, StringId, base::AlreadyHashed<uint64_t>>
      async_tracks_to_root_string_id_;

  TraceProcessorContext* const context_;

  const StringId source_key_;
  const StringId trace_id_is_process_scoped_key_;
  const StringId upid_;
  const StringId source_scope_key_;
  const StringId chrome_source_;
};

}  // namespace perfetto::trace_processor

#endif  // SRC_TRACE_PROCESSOR_IMPORTERS_COMMON_TRACK_COMPRESSOR_H_
