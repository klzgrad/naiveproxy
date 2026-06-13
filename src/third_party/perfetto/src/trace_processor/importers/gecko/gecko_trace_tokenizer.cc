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

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
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

  // Markers (preprocessed format only). Length is the minimum of the array
  // sizes below.
  std::vector<uint32_t> marker_names;      // Index into `strings`.
  std::vector<double> marker_start_times;  // Milliseconds.
  std::vector<double> marker_end_times;    // Milliseconds.
  std::vector<uint8_t> marker_phases;  // 0=Instant, 1=Interval, 2=Start, 3=End.
  std::vector<uint32_t> marker_categories;  // Index into profile categories.
  std::vector<std::string> marker_data;  // Raw JSON for `data`; empty for null.

  bool is_preprocessed = false;
};

namespace {

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

// Reads a uint that may have been emitted as either a JSON number or a numeric
// string (Gecko sometimes serializes pids/tids as strings).
uint32_t ReadUint32OrStringifiedUint32(json::SimpleJsonParser& reader) {
  if (auto v = reader.GetUint32()) {
    return *v;
  }
  if (auto s = reader.GetString()) {
    return base::CStringToUInt32(std::string(*s).c_str()).value_or(0);
  }
  return 0;
}

// Parses `frameTable`, which has two on-the-wire shapes:
//   - Legacy:       { "schema": {...}, "data": [[...], ...] }
//   - Preprocessed: { "func": [...], ... }
base::Status ParseFrameTable(json::SimpleJsonParser& reader, GeckoThread& t) {
  return reader.ForEachField([&](std::string_view key) -> json::FieldResult {
    if (key == "schema" && reader.IsObject()) {
      RETURN_IF_ERROR(ParseSchema(reader, t.frame_table.schema_keys,
                                  t.frame_table.schema_values));
      return json::FieldResult::Handled{};
    }
    if (key == "data" && reader.IsArray()) {
      RETURN_IF_ERROR(ParseDataArray(reader, t.frame_table.data));
      return json::FieldResult::Handled{};
    }
    if (key == "func" && reader.IsArray()) {
      t.is_preprocessed = true;
      if (auto r = reader.CollectUint32Array(); r.ok()) {
        t.frame_func_indices = std::move(*r);
      }
      return json::FieldResult::Handled{};
    }
    return json::FieldResult::Skip{};
  });
}

// Parses the preprocessed `funcTable`. We only care about the `name` array.
base::Status ParseFuncTable(json::SimpleJsonParser& reader, GeckoThread& t) {
  return reader.ForEachField([&](std::string_view key) -> json::FieldResult {
    if (key == "name" && reader.IsArray()) {
      if (auto r = reader.CollectUint32Array(); r.ok()) {
        t.func_names = std::move(*r);
      }
      return json::FieldResult::Handled{};
    }
    return json::FieldResult::Skip{};
  });
}

// Parses `stackTable`, which has two on-the-wire shapes:
//   - Legacy:       { "schema": {...}, "data": [[...], ...] }
//   - Preprocessed: { "prefix": [...], "frame": [...] }
base::Status ParseStackTable(json::SimpleJsonParser& reader, GeckoThread& t) {
  return reader.ForEachField([&](std::string_view key) -> json::FieldResult {
    if (key == "schema" && reader.IsObject()) {
      RETURN_IF_ERROR(ParseSchema(reader, t.stack_table.schema_keys,
                                  t.stack_table.schema_values));
      return json::FieldResult::Handled{};
    }
    if (key == "data" && reader.IsArray()) {
      RETURN_IF_ERROR(ParseDataArray(reader, t.stack_table.data));
      return json::FieldResult::Handled{};
    }
    if (key == "prefix" && reader.IsArray()) {
      t.is_preprocessed = true;
      RETURN_IF_ERROR(ParseOptionalUint32Array(reader, t.stack_prefixes));
      return json::FieldResult::Handled{};
    }
    if (key == "frame" && reader.IsArray()) {
      if (auto r = reader.CollectUint32Array(); r.ok()) {
        t.stack_frames = std::move(*r);
      }
      return json::FieldResult::Handled{};
    }
    return json::FieldResult::Skip{};
  });
}

// Parses `samples`, which has two on-the-wire shapes:
//   - Legacy:       { "schema": {...}, "data": [[...], ...] }
//   - Preprocessed: { "stack": [...], "time": [...] }
base::Status ParseSamplesTable(json::SimpleJsonParser& reader, GeckoThread& t) {
  return reader.ForEachField([&](std::string_view key) -> json::FieldResult {
    if (key == "schema" && reader.IsObject()) {
      RETURN_IF_ERROR(
          ParseSchema(reader, t.samples.schema_keys, t.samples.schema_values));
      return json::FieldResult::Handled{};
    }
    if (key == "data" && reader.IsArray()) {
      RETURN_IF_ERROR(ParseDataArray(reader, t.samples.data));
      return json::FieldResult::Handled{};
    }
    if (key == "stack" && reader.IsArray()) {
      t.is_preprocessed = true;
      RETURN_IF_ERROR(ParseOptionalUint32Array(reader, t.sample_stacks));
      return json::FieldResult::Handled{};
    }
    if (key == "time" && reader.IsArray()) {
      if (auto r = reader.CollectDoubleArray(); r.ok()) {
        t.sample_times = std::move(*r);
      }
      return json::FieldResult::Handled{};
    }
    return json::FieldResult::Skip{};
  });
}

// Captures the raw JSON bytes of each element in the marker `data` array.
// Primitives and nulls are stored as the empty string and skipped at parse
// time; objects/arrays are kept verbatim for later flattening into args.
base::Status ParseMarkerDataArray(json::SimpleJsonParser& reader,
                                  std::vector<std::string>& out) {
  return reader.ForEachArrayElement([&]() {
    if (reader.IsObject() || reader.IsArray()) {
      auto raw = reader.CollectRawObjectOrArray();
      if (!raw.ok()) {
        return raw.status();
      }
      out.emplace_back(*raw);
    } else {
      out.emplace_back();
    }
    return base::OkStatus();
  });
}

// Parses the preprocessed `markers` table. The legacy format is not handled —
// markers were not part of legacy gecko traces in practice.
base::Status ParseMarkersTable(json::SimpleJsonParser& reader, GeckoThread& t) {
  return reader.ForEachField([&](std::string_view key) -> json::FieldResult {
    if (key == "name" && reader.IsArray()) {
      if (auto r = reader.CollectUint32Array(); r.ok()) {
        t.marker_names = std::move(*r);
      }
      return json::FieldResult::Handled{};
    }
    if (key == "startTime" && reader.IsArray()) {
      if (auto r = reader.CollectDoubleArray(); r.ok()) {
        t.marker_start_times = std::move(*r);
      }
      return json::FieldResult::Handled{};
    }
    if (key == "endTime" && reader.IsArray()) {
      if (auto r = reader.CollectDoubleArray(); r.ok()) {
        t.marker_end_times = std::move(*r);
      }
      return json::FieldResult::Handled{};
    }
    if (key == "phase" && reader.IsArray()) {
      if (auto r = reader.CollectUint32Array(); r.ok()) {
        t.marker_phases.reserve(r->size());
        for (uint32_t p : *r) {
          t.marker_phases.push_back(static_cast<uint8_t>(p));
        }
      }
      return json::FieldResult::Handled{};
    }
    if (key == "category" && reader.IsArray()) {
      if (auto r = reader.CollectUint32Array(); r.ok()) {
        t.marker_categories = std::move(*r);
      }
      return json::FieldResult::Handled{};
    }
    if (key == "data" && reader.IsArray()) {
      RETURN_IF_ERROR(ParseMarkerDataArray(reader, t.marker_data));
      return json::FieldResult::Handled{};
    }
    return json::FieldResult::Skip{};
  });
}

// Parse a single thread object. Used for both real threads under `threads` and
// the synthetic thread under `shared`.
base::Status ParseThread(json::SimpleJsonParser& reader, GeckoThread& t) {
  return reader.ForEachField([&](std::string_view key) -> json::FieldResult {
    if (key == "name") {
      t.name = std::string(reader.GetString().value_or(""));
      return json::FieldResult::Handled{};
    }
    if (key == "tid") {
      t.tid = ReadUint32OrStringifiedUint32(reader);
      return json::FieldResult::Handled{};
    }
    if (key == "pid") {
      t.pid = ReadUint32OrStringifiedUint32(reader);
      return json::FieldResult::Handled{};
    }
    if (key == "stringTable" && reader.IsArray()) {
      if (auto r = reader.CollectStringArray(); r.ok()) {
        t.strings = std::move(*r);
      }
      return json::FieldResult::Handled{};
    }
    if (key == "stringArray" && reader.IsArray()) {
      t.is_preprocessed = true;
      if (auto r = reader.CollectStringArray(); r.ok()) {
        t.strings = std::move(*r);
      }
      return json::FieldResult::Handled{};
    }
    if (key == "frameTable" && reader.IsObject()) {
      RETURN_IF_ERROR(ParseFrameTable(reader, t));
      return json::FieldResult::Handled{};
    }
    if (key == "funcTable" && reader.IsObject()) {
      RETURN_IF_ERROR(ParseFuncTable(reader, t));
      return json::FieldResult::Handled{};
    }
    if (key == "stackTable" && reader.IsObject()) {
      RETURN_IF_ERROR(ParseStackTable(reader, t));
      return json::FieldResult::Handled{};
    }
    if (key == "markers" && reader.IsObject()) {
      RETURN_IF_ERROR(ParseMarkersTable(reader, t));
      return json::FieldResult::Handled{};
    }
    if (key == "samples" && reader.IsObject()) {
      RETURN_IF_ERROR(ParseSamplesTable(reader, t));
      return json::FieldResult::Handled{};
    }
    return json::FieldResult::Skip{};
  });
}

struct GeckoProfile {
  std::vector<GeckoThread> threads;
  std::optional<GeckoThread> shared;
  // `name` field of each entry in `meta.categories`. Used to resolve marker
  // category indices to a human-readable string.
  std::vector<std::string> category_names;
};

// Parses `meta.categories`. Each entry is `{name, color, subcategories}`; we
// only keep the `name`.
base::Status ParseMetaCategories(json::SimpleJsonParser& reader,
                                 std::vector<std::string>& out) {
  return reader.ForEachArrayElement([&]() {
    if (!reader.IsObject()) {
      return base::OkStatus();
    }
    std::string name;
    RETURN_IF_ERROR(
        reader.ForEachField([&](std::string_view key) -> json::FieldResult {
          if (key == "name") {
            name = std::string(reader.GetString().value_or(""));
            return json::FieldResult::Handled{};
          }
          return json::FieldResult::Skip{};
        }));
    out.push_back(std::move(name));
    return base::OkStatus();
  });
}

// Parses the `meta` object. We currently only consume `categories`.
base::Status ParseMeta(json::SimpleJsonParser& reader, GeckoProfile& profile) {
  return reader.ForEachField([&](std::string_view key) -> json::FieldResult {
    if (key == "categories" && reader.IsArray()) {
      RETURN_IF_ERROR(ParseMetaCategories(reader, profile.category_names));
      return json::FieldResult::Handled{};
    }
    return json::FieldResult::Skip{};
  });
}

// Parses the `threads` array.
base::Status ParseThreads(json::SimpleJsonParser& reader,
                          std::vector<GeckoThread>& out) {
  return reader.ForEachArrayElement([&]() {
    if (!reader.IsObject()) {
      return base::OkStatus();
    }
    GeckoThread t;
    RETURN_IF_ERROR(ParseThread(reader, t));
    out.push_back(std::move(t));
    return base::OkStatus();
  });
}

// Parse the root gecko profile object.
base::StatusOr<GeckoProfile> ParseGeckoProfile(std::string_view json) {
  GeckoProfile profile;
  json::SimpleJsonParser reader(json);
  RETURN_IF_ERROR(reader.Parse());

  RETURN_IF_ERROR(
      reader.ForEachField([&](std::string_view key) -> json::FieldResult {
        if (key == "meta" && reader.IsObject()) {
          RETURN_IF_ERROR(ParseMeta(reader, profile));
          return json::FieldResult::Handled{};
        }
        if (key == "shared" && reader.IsObject()) {
          profile.shared.emplace();
          RETURN_IF_ERROR(ParseThread(reader, *profile.shared));
          return json::FieldResult::Handled{};
        }
        if (key == "threads" && reader.IsArray()) {
          RETURN_IF_ERROR(ParseThreads(reader, profile.threads));
          return json::FieldResult::Handled{};
        }
        return json::FieldResult::Skip{};
      }));

  return profile;
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
  auto profile_or = ParseGeckoProfile(pending_json_);
  if (!profile_or.ok()) {
    return base::ErrStatus(
        "Syntactic error while parsing Gecko trace: %s; please use an external "
        "JSON tool (e.g. jq) to understand the source of the error.",
        profile_or.status().message().c_str());
  }

  // The `shared` block may carry the string table only, frame+stack tables
  // only, or both. Resolve each of those independently rather than tying them
  // to a single boolean.
  const std::vector<std::string>* shared_strings =
      profile_or->shared ? &profile_or->shared->strings : nullptr;
  const bool shared_has_frames =
      profile_or->shared && (!profile_or->shared->frame_func_indices.empty() ||
                             !profile_or->shared->frame_table.data.empty());

  // Pre-build shared callsites once (reused across threads) when present.
  std::optional<std::vector<Callsite>> shared_callsites;
  if (shared_has_frames) {
    const auto& s = *profile_or->shared;
    shared_callsites = s.is_preprocessed
                           ? ProcessPreprocessedFramesAndStacks(s, s.strings)
                           : ProcessLegacyFramesAndStacks(s, s.strings);
  }

  for (const auto& t : profile_or->threads) {
    // Frame names are resolved against the thread's strings if it has them,
    // otherwise the shared strings (or an empty vector if neither exists).
    const std::vector<std::string>& strings =
        !t.strings.empty() || shared_strings == nullptr ? t.strings
                                                        : *shared_strings;

    if (shared_callsites) {
      if (t.is_preprocessed || profile_or->shared->is_preprocessed) {
        ProcessSamples(t, *shared_callsites);
      } else {
        ProcessLegacySamples(t, *shared_callsites);
      }
    } else if (t.is_preprocessed) {
      auto callsites = ProcessPreprocessedFramesAndStacks(t, strings);
      ProcessSamples(t, callsites);
    } else {
      auto callsites = ProcessLegacyFramesAndStacks(t, strings);
      ProcessLegacySamples(t, callsites);
    }

    ProcessMarkers(t, strings, profile_or->category_names);
  }
  return base::OkStatus();
}

FrameId GeckoTraceTokenizer::InternFrame(base::StringView name) {
  constexpr std::string_view kMappingStart = " (in ";
  size_t mapping_meta_start =
      name.find(base::StringView(kMappingStart.data(), kMappingStart.size()));
  if (mapping_meta_start == base::StringView::npos) {
    if (!dummy_mapping_) {
      dummy_mapping_ = &context_->mapping_tracker->CreateDummyMapping("gecko");
    }
    return dummy_mapping_->InternDummyFrame(name, base::StringView());
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
  return mapping->InternDummyFrame(name.substr(0, mapping_meta_start),
                                   base::StringView());
}

std::vector<Callsite> GeckoTraceTokenizer::ProcessLegacyFramesAndStacks(
    const GeckoThread& t,
    const std::vector<std::string>& strings) {
  std::vector<FrameId> frame_ids;
  std::vector<Callsite> callsites;

  auto location_idx = t.frame_table.GetSchemaIndex("location");
  auto prefix_idx = t.stack_table.GetSchemaIndex("prefix");
  auto frame_idx = t.stack_table.GetSchemaIndex("frame");

  if (!location_idx || !prefix_idx || !frame_idx) {
    return callsites;
  }

  // Process frames.
  for (const auto& frame : t.frame_table.data) {
    if (*location_idx >= frame.size()) {
      continue;
    }
    const auto* loc_val = std::get_if<uint32_t>(&frame[*location_idx]);
    if (!loc_val || *loc_val >= strings.size()) {
      continue;
    }
    frame_ids.push_back(InternFrame(base::StringView(strings[*loc_val])));
  }

  // Process stacks.
  for (const auto& stack : t.stack_table.data) {
    if (*prefix_idx >= stack.size() || *frame_idx >= stack.size()) {
      continue;
    }

    std::optional<CallsiteId> prefix_id;
    uint32_t depth = 0;

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

  return callsites;
}

void GeckoTraceTokenizer::ProcessLegacySamples(
    const GeckoThread& t,
    const std::vector<Callsite>& callsites) {
  auto stack_idx = t.samples.GetSchemaIndex("stack");
  auto time_idx = t.samples.GetSchemaIndex("time");

  if (!stack_idx || !time_idx) {
    return;
  }

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

std::vector<Callsite> GeckoTraceTokenizer::ProcessPreprocessedFramesAndStacks(
    const GeckoThread& t,
    const std::vector<std::string>& strings) {
  std::vector<FrameId> frame_ids;
  std::vector<Callsite> callsites;

  // Process frames using func table indirection.
  for (uint32_t func_idx : t.frame_func_indices) {
    if (func_idx >= t.func_names.size()) {
      continue;
    }
    uint32_t name_str_idx = t.func_names[func_idx];
    if (name_str_idx >= strings.size()) {
      continue;
    }
    frame_ids.push_back(InternFrame(base::StringView(strings[name_str_idx])));
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

  return callsites;
}

void GeckoTraceTokenizer::ProcessSamples(
    const GeckoThread& t,
    const std::vector<Callsite>& callsites) {
  bool added_metadata = false;
  for (size_t i = 0; i < t.sample_stacks.size() && i < t.sample_times.size();
       ++i) {
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

namespace {

// Returns the timestamp (in nanoseconds, in trace-file clock space) at which a
// marker with the given phase should be emitted. IntervalEnd uses `endTime`;
// everything else uses `startTime`.
int64_t MarkerEventTimeNs(GeckoEvent::MarkerPhase phase,
                          double start_ms,
                          double end_ms) {
  double ms =
      phase == GeckoEvent::MarkerPhase::kIntervalEnd ? end_ms : start_ms;
  return static_cast<int64_t>(ms * 1000 * 1000);
}

}  // namespace

void GeckoTraceTokenizer::ProcessMarkers(
    const GeckoThread& t,
    const std::vector<std::string>& strings_for_names,
    const std::vector<std::string>& category_names) {
  // All marker arrays must be index-aligned. Iterate up to the smallest size
  // to ignore any partially-populated tail rows.
  const size_t n = std::min({t.marker_names.size(), t.marker_start_times.size(),
                             t.marker_end_times.size(), t.marker_phases.size(),
                             t.marker_categories.size()});
  if (n == 0) {
    return;
  }

  bool added_metadata = false;
  for (size_t i = 0; i < n; ++i) {
    uint32_t name_idx = t.marker_names[i];
    if (name_idx >= strings_for_names.size()) {
      continue;
    }
    StringPool::Id name_id = context_->storage->InternString(
        base::StringView(strings_for_names[name_idx]));

    StringPool::Id cat_id = StringPool::Id::Null();
    uint32_t cat_idx = t.marker_categories[i];
    if (cat_idx < category_names.size()) {
      cat_id = context_->storage->InternString(
          base::StringView(category_names[cat_idx]));
    }

    auto phase = static_cast<GeckoEvent::MarkerPhase>(t.marker_phases[i]);
    double start_ms = t.marker_start_times[i];
    double end_ms = t.marker_end_times[i];
    int64_t ts = MarkerEventTimeNs(phase, start_ms, end_ms);
    std::optional<int64_t> converted =
        context_->clock_tracker->ToTraceTime(trace_file_clock_, ts);
    if (!converted) {
      continue;
    }

    int64_t dur = phase == GeckoEvent::MarkerPhase::kInterval
                      ? static_cast<int64_t>((end_ms - start_ms) * 1000 * 1000)
                      : 0;

    GeckoEvent::Marker m;
    m.tid = t.tid;
    m.phase = phase;
    m.name = name_id;
    m.category = cat_id;
    m.dur = std::max<int64_t>(0, dur);
    if (i < t.marker_data.size() && !t.marker_data[i].empty()) {
      const auto& s = t.marker_data[i];
      m.data_json = std::make_unique<char[]>(s.size());
      memcpy(m.data_json.get(), s.data(), s.size());
      m.data_json_size = static_cast<uint32_t>(s.size());
    } else {
      m.data_json_size = 0;
    }

    if (!added_metadata) {
      stream_->Push(
          *converted,
          GeckoEvent{GeckoEvent::ThreadMetadata{
              t.tid, t.pid,
              context_->storage->InternString(base::StringView(t.name))}});
      added_metadata = true;
    }
    stream_->Push(*converted, GeckoEvent{std::move(m)});
  }
}

}  // namespace perfetto::trace_processor::gecko_importer
