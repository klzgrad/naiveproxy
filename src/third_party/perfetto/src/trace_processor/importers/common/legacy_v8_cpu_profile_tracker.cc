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

#include "src/trace_processor/importers/common/legacy_v8_cpu_profile_tracker.h"

#include <cstdint>
#include <optional>
#include <utility>
#include <vector>

#include "perfetto/base/logging.h"
#include "perfetto/base/status.h"
#include "perfetto/ext/base/flat_hash_map.h"
#include "perfetto/ext/base/status_or.h"
#include "perfetto/ext/base/string_view.h"
#include "src/trace_processor/importers/common/args_tracker.h"
#include "src/trace_processor/importers/common/mapping_tracker.h"
#include "src/trace_processor/importers/common/parser_types.h"
#include "src/trace_processor/importers/common/process_tracker.h"
#include "src/trace_processor/importers/common/stack_profile_tracker.h"
#include "src/trace_processor/storage/stats.h"
#include "src/trace_processor/storage/trace_storage.h"
#include "src/trace_processor/tables/profiler_tables_py.h"
#include "src/trace_processor/types/trace_processor_context.h"

namespace perfetto::trace_processor {

LegacyV8CpuProfileTracker::LegacyV8CpuProfileTracker(
    TraceProcessorContext* context)
    : context_(context) {}

LegacyV8CpuProfileTracker::~LegacyV8CpuProfileTracker() = default;

void LegacyV8CpuProfileTracker::Parse(int64_t ts,
                                      LegacyV8CpuProfileEvent event) {
  base::Status status =
      AddSample(ts, event.session_id, event.pid, event.tid, event.callsite_id);
  if (!status.ok()) {
    context_->storage->IncrementStats(
        stats::legacy_v8_cpu_profile_invalid_sample);
  }
}

void LegacyV8CpuProfileTracker::SetStartTsForSessionAndPid(uint64_t session_id,
                                                           uint32_t pid,
                                                           int64_t ts) {
  auto [it, inserted] = state_by_session_and_pid_.Insert(
      std::make_pair(session_id, pid),
      State{ts, base::FlatHashMap<uint32_t, CallsiteId>(),
            base::FlatHashMap<uint32_t, uint32_t>(), nullptr});
  it->ts = ts;
  if (inserted) {
    it->mapping = &context_->mapping_tracker->CreateDummyMapping("");
  }
}

base::Status LegacyV8CpuProfileTracker::AddCallsite(
    uint64_t session_id,
    uint32_t pid,
    uint32_t raw_callsite_id,
    std::optional<uint32_t> parent_raw_callsite_id,
    base::StringView script_url,
    base::StringView function_name,
    const std::vector<uint32_t>& raw_children_callsite_ids) {
  auto* state = state_by_session_and_pid_.Find(std::make_pair(session_id, pid));
  if (!state) {
    return base::ErrStatus(
        "v8 profile id does not exist: cannot insert callsite");
  }

  auto* existing_callsite = state->callsites.Find(raw_callsite_id);
  if (existing_callsite) {
    return base::ErrStatus("v8 profile: callsite with id already exists");
  }

  FrameId frame_id =
      state->mapping->InternDummyFrame(function_name, script_url);

  // V8 and NodeJS/DevTools have different formats they expect for parent <->
  // child releationships for stack sampling data.
  //
  // V8 works by providing the parent for every frame, while NodeJS/Devtools
  // follow the devtools protocol [1] which specifies the children. Try to
  // work with either.
  //
  // [1]
  // https://chromedevtools.github.io/devtools-protocol/tot/Profiler/#type-ProfileNode
  if (!parent_raw_callsite_id) {
    auto* parent_ptr = state->callsite_inferred_parents.Find(raw_callsite_id);
    if (parent_ptr) {
      parent_raw_callsite_id = *parent_ptr;
    }
  }

  CallsiteId callsite_id;
  uint32_t depth;
  if (parent_raw_callsite_id) {
    auto* parent_id = state->callsites.Find(*parent_raw_callsite_id);
    if (!parent_id) {
      return base::ErrStatus(
          "v8 profile parent id does not exist: cannot insert callsite");
    }
    auto row =
        context_->storage->stack_profile_callsite_table().FindById(*parent_id);
    callsite_id = context_->stack_profile_tracker->InternCallsite(
        *parent_id, frame_id, row->depth() + 1);
    depth = row->depth() + 1;
  } else {
    callsite_id = context_->stack_profile_tracker->InternCallsite(std::nullopt,
                                                                  frame_id, 0);
    depth = 0;
  }

  // We already asserted above that we don't already have a node with this
  // callsite id.
  PERFETTO_CHECK(state->callsites.Insert(raw_callsite_id, callsite_id).second);

  // Insert the children so it can be picked up if the node is added in the
  // future. Also go through all the nodes in the table itself and fix the
  // parent/depth relationships up if the node is already in the table.
  for (uint32_t raw_child_id : raw_children_callsite_ids) {
    auto [it, inserted] =
        state->callsite_inferred_parents.Insert(raw_child_id, raw_callsite_id);
    if (!inserted) {
      return base::ErrStatus(
          "v8 profile: multiple nodes specify the same node id %u as child",
          raw_child_id);
    }

    auto* child_callsite_id = state->callsites.Find(raw_child_id);

    // This means that we havent' seen the node yet. We expect it to appear in
    // the future and be picked up by the `!parent_raw_callsite_id` above when
    // it does.
    if (!child_callsite_id) {
      continue;
    }
    auto row =
        context_->storage->mutable_stack_profile_callsite_table()->FindById(
            *child_callsite_id);
    PERFETTO_CHECK(row);
    row->set_depth(depth + 1);
    row->set_parent_id(callsite_id);
  }
  return base::OkStatus();
}

base::StatusOr<int64_t> LegacyV8CpuProfileTracker::AddDeltaAndGetTs(
    uint64_t session_id,
    uint32_t pid,
    int64_t delta_ts) {
  auto* state = state_by_session_and_pid_.Find(std::make_pair(session_id, pid));
  if (!state) {
    return base::ErrStatus(
        "v8 profile id does not exist: cannot compute timestamp from delta");
  }
  state->ts += delta_ts;
  return state->ts;
}

base::Status LegacyV8CpuProfileTracker::AddSample(int64_t ts,
                                                  uint64_t session_id,
                                                  uint32_t pid,
                                                  uint32_t tid,
                                                  uint32_t raw_callsite_id) {
  auto* state = state_by_session_and_pid_.Find(std::make_pair(session_id, pid));
  if (!state) {
    return base::ErrStatus("v8 callsite id does not exist: cannot add sample");
  }
  auto* id = state->callsites.Find(raw_callsite_id);
  if (!id) {
    return base::ErrStatus("v8 callsite id does not exist: cannot add sample");
  }
  UniqueTid utid = context_->process_tracker->UpdateThread(tid, pid);
  auto* samples = context_->storage->mutable_cpu_profile_stack_sample_table();
  samples->Insert({ts, *id, utid, 0});
  return base::OkStatus();
}

}  // namespace perfetto::trace_processor
