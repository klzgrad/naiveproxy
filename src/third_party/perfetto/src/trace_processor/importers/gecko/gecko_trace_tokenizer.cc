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

#include "src/trace_processor/importers/gecko/gecko_trace_tokenizer.h"

#include <json/value.h>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "perfetto/base/status.h"
#include "perfetto/ext/base/flat_hash_map.h"
#include "perfetto/ext/base/status_macros.h"
#include "perfetto/ext/base/string_view.h"
#include "perfetto/trace_processor/trace_blob_view.h"
#include "protos/perfetto/trace/clock_snapshot.pbzero.h"
#include "src/trace_processor/importers/common/mapping_tracker.h"
#include "src/trace_processor/importers/common/stack_profile_tracker.h"
#include "src/trace_processor/importers/common/virtual_memory_mapping.h"
#include "src/trace_processor/importers/gecko/gecko_event.h"
#include "src/trace_processor/importers/json/json_utils.h"
#include "src/trace_processor/sorter/trace_sorter.h"
#include "src/trace_processor/storage/trace_storage.h"
#include "src/trace_processor/types/trace_processor_context.h"

namespace perfetto::trace_processor::gecko_importer {
namespace {

struct Callsite {
  CallsiteId id;
  uint32_t depth;
};

}  // namespace

GeckoTraceTokenizer::GeckoTraceTokenizer(TraceProcessorContext* ctx)
    : context_(ctx) {}
GeckoTraceTokenizer::~GeckoTraceTokenizer() = default;

base::Status GeckoTraceTokenizer::Parse(TraceBlobView blob) {
  pending_json_.append(reinterpret_cast<const char*>(blob.data()), blob.size());
  return base::OkStatus();
}

base::Status GeckoTraceTokenizer::NotifyEndOfFile() {
  std::optional<Json::Value> opt_value =
      json::ParseJsonString(base::StringView(pending_json_));
  if (!opt_value) {
    return base::ErrStatus(
        "Syntactic error while Gecko trace; please use an external JSON tool "
        "(e.g. jq) to understand the source of the error.");
  }
  context_->clock_tracker->SetTraceTimeClock(
      protos::pbzero::ClockSnapshot::Clock::MONOTONIC);

  DummyMemoryMapping* dummy_mapping = nullptr;
  base::FlatHashMap<std::string, DummyMemoryMapping*> mappings;

  const Json::Value& value = *opt_value;
  std::vector<FrameId> frame_ids;
  std::vector<Callsite> callsites;
  for (const auto& t : value["threads"]) {
    // The trace uses per-thread indices, we reuse the vector for perf reasons
    // to prevent reallocs on every thread.
    frame_ids.clear();
    callsites.clear();

    const auto& strings = t["stringTable"];
    const auto& frames = t["frameTable"];
    const auto& frames_schema = frames["schema"];
    uint32_t location_idx = frames_schema["location"].asUInt();
    for (const auto& frame : frames["data"]) {
      base::StringView name = strings[frame[location_idx].asUInt()].asCString();

      constexpr std::string_view kMappingStart = " (in ";
      size_t mapping_meta_start = name.find(
          base::StringView(kMappingStart.data(), kMappingStart.size()));
      if (mapping_meta_start == base::StringView::npos &&
          name.data()[name.size() - 1] == ')') {
        if (!dummy_mapping) {
          dummy_mapping =
              &context_->mapping_tracker->CreateDummyMapping("gecko");
        }
        frame_ids.push_back(
            dummy_mapping->InternDummyFrame(name, base::StringView()));
        continue;
      }

      DummyMemoryMapping* mapping;
      size_t mapping_start = mapping_meta_start + kMappingStart.size();
      size_t mapping_end = name.find(')', mapping_start);
      std::string mapping_name =
          name.substr(mapping_start, mapping_end - mapping_start).ToStdString();
      if (auto* mapping_ptr = mappings.Find(mapping_name); mapping_ptr) {
        mapping = *mapping_ptr;
      } else {
        mapping = &context_->mapping_tracker->CreateDummyMapping(mapping_name);
        mappings.Insert(mapping_name, mapping);
      }
      frame_ids.push_back(mapping->InternDummyFrame(
          name.substr(0, mapping_meta_start), base::StringView()));
    }

    const auto& stacks = t["stackTable"];
    const auto& stacks_schema = stacks["schema"];
    uint32_t prefix_index = stacks_schema["prefix"].asUInt();
    uint32_t frame_index = stacks_schema["frame"].asUInt();
    for (const auto& frame : stacks["data"]) {
      const auto& prefix = frame[prefix_index];
      std::optional<CallsiteId> prefix_id;
      uint32_t depth = 0;
      if (!prefix.isNull()) {
        const auto& c = callsites[prefix.asUInt()];
        prefix_id = c.id;
        depth = c.depth + 1;
      }
      CallsiteId cid = context_->stack_profile_tracker->InternCallsite(
          prefix_id, frame_ids[frame[frame_index].asUInt()], depth);
      callsites.push_back({cid, depth});
    }

    const auto& samples = t["samples"];
    const auto& samples_schema = samples["schema"];
    uint32_t stack_index = samples_schema["stack"].asUInt();
    uint32_t time_index = samples_schema["time"].asUInt();
    bool added_metadata = false;
    for (const auto& sample : samples["data"]) {
      uint32_t stack_idx = sample[stack_index].asUInt();
      auto ts =
          static_cast<int64_t>(sample[time_index].asDouble() * 1000 * 1000);
      if (!added_metadata) {
        context_->sorter->PushGeckoEvent(
            ts, GeckoEvent{GeckoEvent::ThreadMetadata{
                    t["tid"].asUInt(), t["pid"].asUInt(),
                    context_->storage->InternString(t["name"].asCString())}});
        added_metadata = true;
      }
      ASSIGN_OR_RETURN(
          int64_t converted,
          context_->clock_tracker->ToTraceTime(
              protos::pbzero::ClockSnapshot::Clock::MONOTONIC, ts));
      context_->sorter->PushGeckoEvent(
          converted, GeckoEvent{GeckoEvent::StackSample{
                         t["tid"].asUInt(), callsites[stack_idx].id}});
    }
  }
  return base::OkStatus();
}

}  // namespace perfetto::trace_processor::gecko_importer
