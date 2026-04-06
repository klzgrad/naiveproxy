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
#include "src/trace_processor/importers/primes/primes_trace_parser.h"

#include <cstdint>

#include "src/trace_processor/importers/common/flow_tracker.h"
#include "src/trace_processor/importers/common/import_logs_tracker.h"
#include "src/trace_processor/importers/common/slice_tracker.h"
#include "src/trace_processor/importers/common/stack_profile_tracker.h"
#include "src/trace_processor/importers/common/track_compressor.h"
#include "src/trace_processor/importers/common/tracks.h"
#include "src/trace_processor/storage/trace_storage.h"
#include "src/trace_processor/types/trace_processor_context.h"

#include "protos/third_party/primes/primes_tracing.pbzero.h"

namespace primespb = perfetto::third_party::primes::pbzero;
namespace tracks = perfetto::trace_processor::tracks;

namespace perfetto::trace_processor::primes {

namespace {

static constexpr auto kExecutorDimension =
    tracks::LongDimensionBlueprint("executor_id");
// Use TrackCompressor::SliceBlueprint for CONCURRENT executors to avoid
// overlapping slices.
static constexpr auto kExecutorCompressorBlueprint =
    TrackCompressor::SliceBlueprint(
        "primes_executor_slice",
        tracks::DimensionBlueprints(kExecutorDimension),
        tracks::DynamicNameBlueprint());

}  // namespace

PrimesTraceParser::PrimesTraceParser(TraceProcessorContext* ctx)
    : context_(ctx),
      edge_id_string_(context_->storage->InternString("edge_id")),
      parent_id_string_(context_->storage->InternString("parent_id")),
      debug_edge_id_(context_->storage->InternString("debug_edge_id")) {}

PrimesTraceParser::~PrimesTraceParser() = default;

void PrimesTraceParser::Parse(int64_t ts, TraceBlobView trace_edge) {
  primespb::TraceEdge::Decoder edge_decoder(trace_edge.data(),
                                            trace_edge.length());

  if (edge_decoder.has_slice_begin()) {
    HandleSliceBegin(ts, edge_decoder);
  } else if (edge_decoder.has_slice_end()) {
    HandleSliceEnd(ts, edge_decoder);
  } else if (edge_decoder.has_mark()) {
    HandleMark(ts, edge_decoder);
  } else {
    context_->import_logs_tracker->RecordParserError(
        stats::primes_unknown_edge_type, ts,
        [&](ArgsTracker::BoundInserter& inserter) {
          inserter.AddArg(
              edge_id_string_,
              Variadic::Integer(static_cast<int64_t>(edge_decoder.id())));
        });
  }
}

void PrimesTraceParser::HandleSliceBegin(
    int64_t ts,
    primespb::TraceEdge_Decoder& edge_decoder) {
  primespb::TraceEdge::SliceBegin::Decoder sb_decoder(
      edge_decoder.slice_begin());
  primespb::TraceEdge::TraceEntityDetails::Decoder details_decoder(
      sb_decoder.entity_details());

  StringId executor_name;
  int64_t executor_id;
  int64_t edge_id = static_cast<int64_t>(edge_decoder.id());
  int64_t parent_id = static_cast<int64_t>(details_decoder.parent_id());

  // A SliceBegin edge may have its own executor_id, indicating that it is the
  // root slice for the executor (the first slice that runs on a particular
  // executor). Otherwise, retrieve `executor_id` by finding this edge's parent.
  if (sb_decoder.has_executor_id()) {
    executor_id = static_cast<int64_t>(sb_decoder.executor_id());
    executor_name = context_->storage->InternString(sb_decoder.executor_name());
  } else if (details_decoder.has_parent_id()) {
    auto* it = edge_to_executor_map_.Find(parent_id);
    if (!it) {
      context_->import_logs_tracker->RecordParserError(
          stats::primes_executor_not_found, ts,
          [&](ArgsTracker::BoundInserter& inserter) {
            inserter.AddArg(edge_id_string_, Variadic::Integer(edge_id));
            inserter.AddArg(parent_id_string_, Variadic::Integer(parent_id));
          });
      return;
    }
    executor_id = *it;
    executor_name = kNullStringId;
  } else {
    context_->import_logs_tracker->RecordParserError(
        stats::primes_missing_parent_id, ts,
        [&](ArgsTracker::BoundInserter& inserter) {
          inserter.AddArg(edge_id_string_, Variadic::Integer(edge_id));
        });
    return;
  }
  // Keep track of which edges are on which executors so that future edges can
  // use their parent_id to look up their executor.
  edge_to_executor_map_[edge_id] = executor_id;

  TrackId track_id = context_->track_compressor->InternBegin(
      kExecutorCompressorBlueprint, tracks::Dimensions(executor_id), edge_id,
      executor_name);

  // Now that an appropriate track for this slice has been found, begin a slice
  // on that track.
  StringId slice_name = context_->storage->InternString(details_decoder.name());

  std::optional<SliceId> slice_id =
      context_->slice_tracker->Begin(ts, track_id, kNullStringId, slice_name);

  if (!slice_id) {
    return;
  }

  // Register this slice as a potential flow source.
  context_->flow_tracker->Begin(track_id,
                                static_cast<FlowId>(edge_decoder.id()));
  HandleFlows(track_id, details_decoder);
}

void PrimesTraceParser::HandleSliceEnd(
    int64_t ts,
    primespb::TraceEdge_Decoder& edge_decoder) {
  int64_t edge_id = static_cast<int64_t>(edge_decoder.id());
  // A SliceEnd edge has the same ID as the corresponding SliceBegin edge.
  int64_t* executor_id = edge_to_executor_map_.Find(edge_id);
  if (!executor_id) {
    context_->import_logs_tracker->RecordParserError(
        stats::primes_end_without_matching_begin, ts,
        [&](ArgsTracker::BoundInserter& inserter) {
          inserter.AddArg(edge_id_string_, Variadic::Integer(edge_id));
        });
    return;
  }
  // If this slice was managed by the track compressor, we need to notify it
  // that the slice has ended so it can reuse the track.
  TrackId track_id = context_->track_compressor->InternEnd(
      kExecutorCompressorBlueprint, tracks::Dimensions(*executor_id), edge_id,
      kNullStringId);
  context_->slice_tracker->End(ts, track_id, kNullStringId, kNullStringId);
}

void PrimesTraceParser::HandleMark(int64_t ts,
                                   primespb::TraceEdge_Decoder& edge_decoder) {
  int64_t edge_id = static_cast<int64_t>(edge_decoder.id());
  auto mark_decoder = primespb::TraceEdge_Mark_Decoder(edge_decoder.mark());
  if (!mark_decoder.has_entity_details()) {
    context_->import_logs_tracker->RecordParserError(
        stats::primes_missing_entity_details, ts,
        [&](ArgsTracker::BoundInserter& inserter) {
          inserter.AddArg(edge_id_string_, Variadic::Integer(edge_id));
        });
    return;
  }
  auto details_decoder = primespb::TraceEdge_TraceEntityDetails_Decoder(
      mark_decoder.entity_details());
  if (!details_decoder.has_parent_id()) {
    context_->import_logs_tracker->RecordParserError(
        stats::primes_missing_parent_id, ts,
        [&](ArgsTracker::BoundInserter& inserter) {
          inserter.AddArg(edge_id_string_, Variadic::Integer(edge_id));
        });
    return;
  }

  // A mark does not ever open an executor, so it must have a parent.
  int64_t parent_id = static_cast<int64_t>(details_decoder.parent_id());
  auto* executor_id = edge_to_executor_map_.Find(parent_id);
  if (!executor_id) {
    context_->import_logs_tracker->RecordParserError(
        stats::primes_executor_not_found, ts,
        [&](ArgsTracker::BoundInserter& inserter) {
          inserter.AddArg(debug_edge_id_, Variadic::Integer(edge_id));
        });
    return;
  }

  // Determine an appropriate track for this mark using TrackCompressor.
  TrackId track_id = context_->track_compressor->InternBegin(
      kExecutorCompressorBlueprint, tracks::Dimensions(*executor_id), edge_id,
      kNullStringId);

  // A mark is a slice with zero duration. Begin a slice with 0 duration on the
  // track found above.
  auto slice_name = context_->storage->InternString(details_decoder.name());
  std::optional<SliceId> slice_id = context_->slice_tracker->Scoped(
      ts, track_id, kNullStringId, slice_name, 0);
  if (!slice_id) {
    return;
  }

  // Register this mark as a potential flow source.
  context_->flow_tracker->Begin(track_id,
                                static_cast<FlowId>(edge_decoder.id()));
  HandleFlows(track_id, details_decoder);
}

// Handles both "follows_from" relationships (which are direct, causal links
// between two specific slices, A -> B) and "flow_ids" (which are shared
// identifiers linking a chain of events across threads/processes,
// e.g., A -> B -> C).
//
// For follows_from: Creates a direct flow from the leader slice to the current
// slice. For flow_ids: Manages the flow chain state (Begin/Step) to link the
// current slice to the previous slice in the same flow chain.
void PrimesTraceParser::HandleFlows(
    TrackId track_id,
    const primespb::TraceEdge_TraceEntityDetails_Decoder& details_decoder) {
  // Convert follows-from relationships into flows.
  if (details_decoder.has_follows_from_ids()) {
    for (auto it = details_decoder.follows_from_ids(); it; ++it) {
      uint64_t follows_from_id = it->as_uint64();
      // Connect the flow from the leader to the current slice (which is
      // enclosing/open on track_id).
      context_->flow_tracker->End(track_id,
                                  static_cast<FlowId>(follows_from_id),
                                  /*bind_enclosing_slice=*/true,
                                  /*close_flow=*/false);
    }
  }

  if (details_decoder.has_flow_ids()) {
    for (auto it = details_decoder.flow_ids(); it; ++it) {
      uint64_t flow_id = it->as_uint64();
      if (context_->flow_tracker->IsActive(flow_id)) {
        context_->flow_tracker->Step(track_id, flow_id);
      } else {
        context_->flow_tracker->Begin(track_id, flow_id);
      }
    }
  }
}

}  // namespace perfetto::trace_processor::primes
