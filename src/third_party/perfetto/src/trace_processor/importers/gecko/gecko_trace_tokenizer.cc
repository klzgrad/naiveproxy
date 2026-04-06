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

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

#include "perfetto/base/status.h"
#include "perfetto/ext/base/flat_hash_map.h"
#include "perfetto/ext/base/status_macros.h"
#include "perfetto/ext/base/status_or.h"
#include "perfetto/ext/base/string_utils.h"
#include "perfetto/ext/base/string_view.h"
#include "perfetto/trace_processor/trace_blob_view.h"
#include "src/trace_processor/importers/common/clock_tracker.h"
#include "src/trace_processor/importers/common/mapping_tracker.h"
#include "src/trace_processor/importers/common/stack_profile_tracker.h"
#include "src/trace_processor/importers/common/virtual_memory_mapping.h"
#include "src/trace_processor/importers/gecko/gecko_event.h"
#include "src/trace_processor/importers/gecko/gecko_trace_parser.h"
#include "src/trace_processor/storage/trace_storage.h"
#include "src/trace_processor/types/trace_processor_context.h"
#include "src/trace_processor/util/clock_synchronizer.h"
#include "src/trace_processor/util/simple_json_parser.h"

namespace perfetto::trace_processor::gecko_importer {

// Parsed gecko thread data (forward declared in header).
struct GeckoThread {
  std::string name;
  uint32_t tid = 0;
  uint32_t pid = 0;

  // String table (either stringTable for legacy or stringArray for
  // preprocessed)
  std::vector<std::string> strings;

  // Legacy format: schema-based tables with data arrays.
  struct LegacyTable {
    std::vector<std::string> schema_keys;
    std::vector<uint32_t> schema_values;
    std::vector<std::vector<std::variant<uint32_t, double, std::monostate>>>
        data;

    std::optional<uint32_t> GetSchemaIndex(std::string_view key) const {
      for (size_t i = 0; i < schema_keys.size(); ++i) {
        if (schema_keys[i] == key) {
          return schema_values[i];
        }
      }
      return std::nullopt;
    }
  };
  LegacyTable frame_table;
  LegacyTable stack_table;
  LegacyTable samples;

  // Preprocessed format: flat arrays.
  std::vector<uint32_t> frame_func_indices;
  std::vector<uint32_t> func_names;
  std::vector<std::optional<uint32_t>> stack_prefixes;
  std::vector<uint32_t> stack_frames;
  std::vector<std::optional<uint32_t>> sample_stacks;
  std::vector<double> sample_times;

  bool is_preprocessed = false;
};

namespace {

struct Callsite {
  CallsiteId id;
  uint32_t depth;
};

// Parse a schema object: {"field1": index1, "field2": index2, ...}
base::Status ParseSchema(json::SimpleJsonParser& reader,
                         std::vector<std::string>& keys,
                         std::vector<uint32_t>& values) {
  return reader.ForEachField([&](std::string_view key) -> json::FieldResult {
    keys.emplace_back(key);
    values.push_back(reader.GetUint32().value_or(0));
    return json::FieldResult::Handled{};
  });
}

// Parse a data array where each element is an array of values.
base::Status ParseDataArray(
    json::SimpleJsonParser& reader,
    std::vector<std::vector<std::variant<uint32_t, double, std::monostate>>>&
        data) {
  return reader.ForEachArrayElement([&]() {
    if (!reader.IsArray()) {
      return base::OkStatus();
    }
    std::vector<std::variant<uint32_t, double, std::monostate>> row;
    RETURN_IF_ERROR(reader.ForEachArrayElement([&]() {
      if (reader.IsNull()) {
        row.push_back(std::monostate{});
      } else if (auto v = reader.GetUint32()) {
        row.push_back(*v);
      } else if (auto d = reader.GetDouble()) {
        row.push_back(*d);
      } else {
        row.push_back(std::monostate{});
      }
      return base::OkStatus();
    }));
    data.push_back(std::move(row));
    return base::OkStatus();
  });
}

// Parse optional uint32 array (elements can be null).
base::Status ParseOptionalUint32Array(
    json::SimpleJsonParser& reader,
    std::vector<std::optional<uint32_t>>& out) {
  return reader.ForEachArrayElement([&]() {
    if (reader.IsNull()) {
      out.emplace_back(std::nullopt);
    } else {
      out.push_back(reader.GetUint32());
    }
    return base::OkStatus();
  });
}

// Parse a single thread object.
base::Status ParseThread(json::SimpleJsonParser& reader, GeckoThread& t) {
  return reader.ForEachField([&](std::string_view key) -> json::FieldResult {
    if (key == "name") {
      t.name = std::string(reader.GetString().value_or(""));
      return json::FieldResult::Handled{};
    }
    if (key == "tid") {
      if (auto v = reader.GetUint32()) {
        t.tid = *v;
      } else if (auto s = reader.GetString()) {
        t.tid = base::CStringToUInt32(std::string(*s).c_str()).value_or(0);
      }
      return json::FieldResult::Handled{};
    }
    if (key == "pid") {
      if (auto v = reader.GetUint32()) {
        t.pid = *v;
      } else if (auto s = reader.GetString()) {
        t.pid = base::CStringToUInt32(std::string(*s).c_str()).value_or(0);
      }
      return json::FieldResult::Handled{};
    }
    if (key == "stringTable" && reader.IsArray()) {
      // Legacy format string table.
      auto result = reader.CollectStringArray();
      if (result.ok()) {
        t.strings = std::move(*result);
      }
      return json::FieldResult::Handled{};
    }
    if (key == "stringArray" && reader.IsArray()) {
      // Preprocessed format string array.
      t.is_preprocessed = true;
      auto result = reader.CollectStringArray();
      if (result.ok()) {
        t.strings = std::move(*result);
      }
      return json::FieldResult::Handled{};
    }
    if (key == "frameTable" && reader.IsObject()) {
      // Check if this is preprocessed (has "func" array) or legacy (has
      // "schema").
      RETURN_IF_ERROR(
          reader.ForEachField([&](std::string_view fkey) -> json::FieldResult {
            if (fkey == "schema" && reader.IsObject()) {
              RETURN_IF_ERROR(ParseSchema(reader, t.frame_table.schema_keys,
                                          t.frame_table.schema_values));
              return json::FieldResult::Handled{};
            }
            if (fkey == "data" && reader.IsArray()) {
              RETURN_IF_ERROR(ParseDataArray(reader, t.frame_table.data));
              return json::FieldResult::Handled{};
            }
            if (fkey == "func" && reader.IsArray()) {
              t.is_preprocessed = true;
              auto result = reader.CollectUint32Array();
              if (result.ok()) {
                t.frame_func_indices = std::move(*result);
              }
              return json::FieldResult::Handled{};
            }
            return json::FieldResult::Skip{};
          }));
      return json::FieldResult::Handled{};
    }
    if (key == "funcTable" && reader.IsObject()) {
      // Preprocessed format function table.
      RETURN_IF_ERROR(
          reader.ForEachField([&](std::string_view fkey) -> json::FieldResult {
            if (fkey == "name" && reader.IsArray()) {
              auto result = reader.CollectUint32Array();
              if (result.ok()) {
                t.func_names = std::move(*result);
              }
              return json::FieldResult::Handled{};
            }
            return json::FieldResult::Skip{};
          }));
      return json::FieldResult::Handled{};
    }
    if (key == "stackTable" && reader.IsObject()) {
      RETURN_IF_ERROR(
          reader.ForEachField([&](std::string_view skey) -> json::FieldResult {
            if (skey == "schema" && reader.IsObject()) {
              RETURN_IF_ERROR(ParseSchema(reader, t.stack_table.schema_keys,
                                          t.stack_table.schema_values));
              return json::FieldResult::Handled{};
            }
            if (skey == "data" && reader.IsArray()) {
              RETURN_IF_ERROR(ParseDataArray(reader, t.stack_table.data));
              return json::FieldResult::Handled{};
            }
            if (skey == "prefix" && reader.IsArray()) {
              t.is_preprocessed = true;
              RETURN_IF_ERROR(
                  ParseOptionalUint32Array(reader, t.stack_prefixes));
              return json::FieldResult::Handled{};
            }
            if (skey == "frame" && reader.IsArray()) {
              auto result = reader.CollectUint32Array();
              if (result.ok()) {
                t.stack_frames = std::move(*result);
              }
              return json::FieldResult::Handled{};
            }
            return json::FieldResult::Skip{};
          }));
      return json::FieldResult::Handled{};
    }
    if (key == "samples" && reader.IsObject()) {
      RETURN_IF_ERROR(
          reader.ForEachField([&](std::string_view skey) -> json::FieldResult {
            if (skey == "schema" && reader.IsObject()) {
              RETURN_IF_ERROR(ParseSchema(reader, t.samples.schema_keys,
                                          t.samples.schema_values));
              return json::FieldResult::Handled{};
            }
            if (skey == "data" && reader.IsArray()) {
              RETURN_IF_ERROR(ParseDataArray(reader, t.samples.data));
              return json::FieldResult::Handled{};
            }
            if (skey == "stack" && reader.IsArray()) {
              t.is_preprocessed = true;
              RETURN_IF_ERROR(
                  ParseOptionalUint32Array(reader, t.sample_stacks));
              return json::FieldResult::Handled{};
            }
            if (skey == "time" && reader.IsArray()) {
              auto result = reader.CollectDoubleArray();
              if (result.ok()) {
                t.sample_times = std::move(*result);
              }
              return json::FieldResult::Handled{};
            }
            return json::FieldResult::Skip{};
          }));
      return json::FieldResult::Handled{};
    }
    return json::FieldResult::Skip{};
  });
}

// Parse the root gecko profile object.
base::StatusOr<std::vector<GeckoThread>> ParseGeckoProfile(
    std::string_view json) {
  std::vector<GeckoThread> threads;
  json::SimpleJsonParser reader(json);
  RETURN_IF_ERROR(reader.Parse());

  RETURN_IF_ERROR(
      reader.ForEachField([&](std::string_view key) -> json::FieldResult {
        if (key == "threads" && reader.IsArray()) {
          RETURN_IF_ERROR(reader.ForEachArrayElement([&]() {
            if (reader.IsObject()) {
              GeckoThread t;
              RETURN_IF_ERROR(ParseThread(reader, t));
              threads.push_back(std::move(t));
            }
            return base::OkStatus();
          }));
          return json::FieldResult::Handled{};
        }
        return json::FieldResult::Skip{};
      }));

  return threads;
}

}  // namespace

GeckoTraceTokenizer::GeckoTraceTokenizer(TraceProcessorContext* ctx)
    : context_(ctx),
      stream_(
          ctx->sorter->CreateStream(std::make_unique<GeckoTraceParser>(ctx))),
      trace_file_clock_(ClockId::TraceFile(ctx->trace_id().value)) {}
GeckoTraceTokenizer::~GeckoTraceTokenizer() = default;

base::Status GeckoTraceTokenizer::Parse(TraceBlobView blob) {
  pending_json_.append(reinterpret_cast<const char*>(blob.data()), blob.size());
  return base::OkStatus();
}

base::Status GeckoTraceTokenizer::OnPushDataToSorter() {
  auto threads_or = ParseGeckoProfile(pending_json_);
  if (!threads_or.ok()) {
    return base::ErrStatus(
        "Syntactic error while parsing Gecko trace: %s; please use an external "
        "JSON tool (e.g. jq) to understand the source of the error.",
        threads_or.status().message().c_str());
  }

  for (const auto& t : *threads_or) {
    if (t.is_preprocessed) {
      ProcessPreprocessedThread(t);
    } else {
      ProcessLegacyThread(t);
    }
  }
  return base::OkStatus();
}

void GeckoTraceTokenizer::ProcessLegacyThread(const GeckoThread& t) {
  std::vector<FrameId> frame_ids;
  std::vector<Callsite> callsites;

  // Get schema indices.
  auto location_idx = t.frame_table.GetSchemaIndex("location");
  auto prefix_idx = t.stack_table.GetSchemaIndex("prefix");
  auto frame_idx = t.stack_table.GetSchemaIndex("frame");
  auto stack_idx = t.samples.GetSchemaIndex("stack");
  auto time_idx = t.samples.GetSchemaIndex("time");

  if (!location_idx || !prefix_idx || !frame_idx || !stack_idx || !time_idx) {
    return;
  }

  // Process frames.
  for (const auto& frame : t.frame_table.data) {
    if (*location_idx >= frame.size()) {
      continue;
    }
    const auto* loc_val = std::get_if<uint32_t>(&frame[*location_idx]);
    if (!loc_val || *loc_val >= t.strings.size()) {
      continue;
    }
    base::StringView name(t.strings[*loc_val]);

    constexpr std::string_view kMappingStart = " (in ";
    size_t mapping_meta_start =
        name.find(base::StringView(kMappingStart.data(), kMappingStart.size()));
    if (mapping_meta_start == base::StringView::npos && !name.empty() &&
        name.data()[name.size() - 1] == ')') {
      if (!dummy_mapping_) {
        dummy_mapping_ =
            &context_->mapping_tracker->CreateDummyMapping("gecko");
      }
      frame_ids.push_back(
          dummy_mapping_->InternDummyFrame(name, base::StringView()));
      continue;
    }

    DummyMemoryMapping* mapping;
    size_t mapping_start = mapping_meta_start + kMappingStart.size();
    size_t mapping_end = name.find(')', mapping_start);
    std::string mapping_name =
        name.substr(mapping_start, mapping_end - mapping_start).ToStdString();
    if (auto* mapping_ptr = mappings_.Find(mapping_name); mapping_ptr) {
      mapping = *mapping_ptr;
    } else {
      mapping = &context_->mapping_tracker->CreateDummyMapping(mapping_name);
      mappings_.Insert(mapping_name, mapping);
    }
    frame_ids.push_back(mapping->InternDummyFrame(
        name.substr(0, mapping_meta_start), base::StringView()));
  }

  // Process stacks.
  for (const auto& stack : t.stack_table.data) {
    if (*prefix_idx >= stack.size() || *frame_idx >= stack.size()) {
      continue;
    }

    std::optional<CallsiteId> prefix_id;
    uint32_t depth = 0;

    // Check if prefix is not null.
    if (const auto* prefix_val = std::get_if<uint32_t>(&stack[*prefix_idx])) {
      if (*prefix_val < callsites.size()) {
        const auto& c = callsites[*prefix_val];
        prefix_id = c.id;
        depth = c.depth + 1;
      }
    }

    const auto* frame_val = std::get_if<uint32_t>(&stack[*frame_idx]);
    if (!frame_val || *frame_val >= frame_ids.size()) {
      continue;
    }

    CallsiteId cid = context_->stack_profile_tracker->InternCallsite(
        prefix_id, frame_ids[*frame_val], depth);
    callsites.push_back({cid, depth});
  }

  // Process samples.
  bool added_metadata = false;
  for (const auto& sample : t.samples.data) {
    if (*stack_idx >= sample.size() || *time_idx >= sample.size()) {
      continue;
    }

    const auto* stack_val = std::get_if<uint32_t>(&sample[*stack_idx]);
    if (!stack_val || *stack_val >= callsites.size()) {
      continue;
    }

    double time_val = 0;
    if (const auto* d = std::get_if<double>(&sample[*time_idx])) {
      time_val = *d;
    } else if (const auto* u = std::get_if<uint32_t>(&sample[*time_idx])) {
      time_val = static_cast<double>(*u);
    }

    auto ts = static_cast<int64_t>(time_val * 1000 * 1000);
    std::optional<int64_t> converted =
        context_->clock_tracker->ToTraceTime(trace_file_clock_, ts);
    if (!converted) {
      continue;
    }
    if (!added_metadata) {
      stream_->Push(
          *converted,
          GeckoEvent{GeckoEvent::ThreadMetadata{
              t.tid, t.pid,
              context_->storage->InternString(base::StringView(t.name))}});
      added_metadata = true;
    }
    stream_->Push(*converted, GeckoEvent{GeckoEvent::StackSample{
                                  t.tid, callsites[*stack_val].id}});
  }
}

void GeckoTraceTokenizer::ProcessPreprocessedThread(const GeckoThread& t) {
  std::vector<FrameId> frame_ids;
  std::vector<Callsite> callsites;

  // Process frames using func table indirection.
  for (uint32_t func_idx : t.frame_func_indices) {
    if (func_idx >= t.func_names.size()) {
      continue;
    }
    uint32_t name_str_idx = t.func_names[func_idx];
    if (name_str_idx >= t.strings.size()) {
      continue;
    }
    base::StringView name(t.strings[name_str_idx]);

    constexpr std::string_view kMappingStart = " (in ";
    size_t mapping_meta_start =
        name.find(base::StringView(kMappingStart.data(), kMappingStart.size()));
    if (mapping_meta_start == base::StringView::npos) {
      if (!dummy_mapping_) {
        dummy_mapping_ =
            &context_->mapping_tracker->CreateDummyMapping("gecko");
      }
      frame_ids.push_back(
          dummy_mapping_->InternDummyFrame(name, base::StringView()));
      continue;
    }

    DummyMemoryMapping* mapping;
    size_t mapping_start = mapping_meta_start + kMappingStart.size();
    size_t mapping_end = name.find(')', mapping_start);
    std::string mapping_name =
        name.substr(mapping_start, mapping_end - mapping_start).ToStdString();
    if (auto* mapping_ptr = mappings_.Find(mapping_name); mapping_ptr) {
      mapping = *mapping_ptr;
    } else {
      mapping = &context_->mapping_tracker->CreateDummyMapping(mapping_name);
      mappings_.Insert(mapping_name, mapping);
    }
    frame_ids.push_back(mapping->InternDummyFrame(
        name.substr(0, mapping_meta_start), base::StringView()));
  }

  // Process stacks using separate prefix/frame arrays.
  for (size_t i = 0; i < t.stack_prefixes.size() && i < t.stack_frames.size();
       ++i) {
    std::optional<CallsiteId> prefix_id;
    uint32_t depth = 0;

    if (t.stack_prefixes[i].has_value()) {
      uint32_t prefix_idx = *t.stack_prefixes[i];
      if (prefix_idx < callsites.size()) {
        const auto& c = callsites[prefix_idx];
        prefix_id = c.id;
        depth = c.depth + 1;
      }
    }

    uint32_t frame_val = t.stack_frames[i];
    if (frame_val >= frame_ids.size()) {
      continue;
    }

    CallsiteId cid = context_->stack_profile_tracker->InternCallsite(
        prefix_id, frame_ids[frame_val], depth);
    callsites.push_back({cid, depth});
  }

  // Process samples using separate stack/time arrays.
  bool added_metadata = false;
  for (size_t i = 0; i < t.sample_stacks.size() && i < t.sample_times.size();
       ++i) {
    // Stack can be null in preprocessed format.
    if (!t.sample_stacks[i].has_value()) {
      continue;
    }
    uint32_t stack_idx = *t.sample_stacks[i];
    if (stack_idx >= callsites.size()) {
      continue;
    }

    auto ts = static_cast<int64_t>(t.sample_times[i] * 1000 * 1000);
    std::optional<int64_t> converted =
        context_->clock_tracker->ToTraceTime(trace_file_clock_, ts);
    if (!converted) {
      continue;
    }
    if (!added_metadata) {
      stream_->Push(
          *converted,
          GeckoEvent{GeckoEvent::ThreadMetadata{
              t.tid, t.pid,
              context_->storage->InternString(base::StringView(t.name))}});
      added_metadata = true;
    }
    stream_->Push(*converted, GeckoEvent{GeckoEvent::StackSample{
                                  t.tid, callsites[stack_idx].id}});
  }
}

}  // namespace perfetto::trace_processor::gecko_importer
