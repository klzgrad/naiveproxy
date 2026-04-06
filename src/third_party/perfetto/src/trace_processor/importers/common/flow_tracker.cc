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

#include <limits>
#include <optional>

#include <stdint.h>

#include "src/trace_processor/importers/common/flow_tracker.h"
#include "src/trace_processor/importers/common/slice_tracker.h"
#include "src/trace_processor/storage/stats.h"
#include "src/trace_processor/storage/trace_storage.h"
#include "src/trace_processor/tables/flow_tables_py.h"
#include "src/trace_processor/types/trace_processor_context.h"
#include "src/trace_processor/types/variadic.h"

namespace perfetto {
namespace trace_processor {

FlowTracker::FlowTracker(TraceProcessorContext* context) : context_(context) {
  name_key_id_ = context_->storage->InternString("name");
  cat_key_id_ = context_->storage->InternString("cat");
}

FlowTracker::~FlowTracker() = default;

/* TODO: if we report a flow event earlier that a corresponding slice then
  flow event would not be added, and it will increase "flow_no_enclosing_slice"
  In catapult, it was possible to report a flow after an enclosing slice if
  timestamps were equal. But because of our sequential processing of a trace
  it is a bit tricky to make it here.
  We suspect that this case is too rare or impossible */
void FlowTracker::Begin(TrackId track_id, FlowId flow_id) {
  std::optional<SliceId> open_slice_id =
      context_->slice_tracker->GetTopmostSliceOnTrack(track_id);
  if (!open_slice_id) {
    context_->storage->IncrementStats(stats::flow_no_enclosing_slice);
    return;
  }
  Begin(open_slice_id.value(), flow_id);
}

void FlowTracker::Begin(SliceId slice_id, FlowId flow_id) {
  auto it_and_ins = flow_to_slice_map_.Insert(flow_id, slice_id);
  if (!it_and_ins.second) {
    context_->storage->IncrementStats(stats::flow_duplicate_id);
    return;
  }
}

void FlowTracker::Step(TrackId track_id, FlowId flow_id) {
  std::optional<SliceId> open_slice_id =
      context_->slice_tracker->GetTopmostSliceOnTrack(track_id);
  if (!open_slice_id) {
    context_->storage->IncrementStats(stats::flow_no_enclosing_slice);
    return;
  }
  Step(open_slice_id.value(), flow_id);
}

void FlowTracker::Step(SliceId new_id, FlowId flow_id) {
  auto* it = flow_to_slice_map_.Find(flow_id);
  if (!it) {
    context_->storage->IncrementStats(stats::flow_step_without_start);
    return;
  }
  SliceId existing_id = *it;
  int64_t existing_ts =
      context_->storage->slice_table().FindById(existing_id)->ts();
  int64_t new_ts = context_->storage->slice_table().FindById(new_id)->ts();
  SliceId outgoing = existing_ts > new_ts ? new_id : existing_id;
  SliceId incoming = existing_ts <= new_ts ? new_id : existing_id;
  InsertFlow(flow_id, outgoing, incoming);
  *it = new_id;
}

void FlowTracker::End(TrackId track_id,
                      FlowId flow_id,
                      bool bind_enclosing_slice,
                      bool close_flow) {
  if (!bind_enclosing_slice) {
    pending_flow_ids_map_[track_id].push_back(flow_id);
    return;
  }
  std::optional<SliceId> open_slice_id =
      context_->slice_tracker->GetTopmostSliceOnTrack(track_id);
  if (!open_slice_id) {
    context_->storage->IncrementStats(stats::flow_no_enclosing_slice);
    return;
  }
  End(open_slice_id.value(), flow_id, close_flow);
}

void FlowTracker::End(SliceId new_id, FlowId flow_id, bool close_flow) {
  auto* it = flow_to_slice_map_.Find(flow_id);
  if (!it) {
    context_->storage->IncrementStats(stats::flow_end_without_start);
    return;
  }
  SliceId existing_id = *it;
  int64_t existing_ts =
      context_->storage->slice_table().FindById(existing_id)->ts();
  int64_t new_ts = context_->storage->slice_table().FindById(new_id)->ts();
  SliceId outgoing = existing_ts > new_ts ? new_id : existing_id;
  SliceId incoming = existing_ts <= new_ts ? new_id : existing_id;
  if (close_flow)
    flow_to_slice_map_.Erase(flow_id);
  InsertFlow(flow_id, outgoing, incoming);
}

bool FlowTracker::IsActive(FlowId flow_id) const {
  return flow_to_slice_map_.Find(flow_id) != nullptr;
}

FlowId FlowTracker::GetFlowIdForV1Event(uint64_t source_id,
                                        StringId cat,
                                        StringId name) {
  V1FlowId v1_flow_id = {source_id, cat, name};
  auto* iter = v1_flow_id_to_flow_id_map_.Find(v1_flow_id);
  if (iter)
    return *iter;
  FlowId new_id = v1_id_counter_++;
  flow_id_to_v1_flow_id_map_[new_id] = v1_flow_id;
  v1_flow_id_to_flow_id_map_[v1_flow_id] = new_id;
  return new_id;
}

void FlowTracker::ClosePendingEventsOnTrack(TrackId track_id,
                                            SliceId slice_id) {
  auto* iter = pending_flow_ids_map_.Find(track_id);
  if (!iter)
    return;

  for (FlowId flow_id : *iter) {
    SliceId slice_out_id = flow_to_slice_map_[flow_id];
    InsertFlow(flow_id, slice_out_id, slice_id);
  }

  pending_flow_ids_map_.Erase(track_id);
}

void FlowTracker::InsertFlow(FlowId flow_id,
                             SliceId slice_out_id,
                             SliceId slice_in_id) {
  tables::FlowTable::Row row(slice_out_id, slice_in_id, flow_id, std::nullopt);
  auto id = context_->storage->mutable_flow_table()->Insert(row).id;

  auto* it = flow_id_to_v1_flow_id_map_.Find(flow_id);
  if (it) {
    // TODO(b/168007725): Add any args from v1 flow events and also export them.
    ArgsTracker args_tracker(context_);
    auto inserter = args_tracker.AddArgsTo(id);
    inserter.AddArg(name_key_id_, Variadic::String(it->name));
    inserter.AddArg(cat_key_id_, Variadic::String(it->cat));
  }
}

void FlowTracker::InsertFlow(SliceId slice_out_id, SliceId slice_in_id) {
  tables::FlowTable::Row row(slice_out_id, slice_in_id, std::nullopt,
                             std::nullopt);
  context_->storage->mutable_flow_table()->Insert(row);
}

}  // namespace trace_processor
}  // namespace perfetto
