/*
 * Copyright (C) 2025 The Android Open Source Project
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

#include "src/trace_processor/importers/pprof/pprof_trace_reader.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "perfetto/base/status.h"
#include "perfetto/ext/base/flat_hash_map.h"
#include "perfetto/ext/base/string_view.h"
#include "perfetto/protozero/proto_utils.h"
#include "perfetto/trace_processor/trace_blob_view.h"
#include "src/trace_processor/importers/common/address_range.h"
#include "src/trace_processor/importers/common/create_mapping_params.h"
#include "src/trace_processor/importers/common/mapping_tracker.h"
#include "src/trace_processor/importers/common/stack_profile_tracker.h"
#include "src/trace_processor/importers/common/virtual_memory_mapping.h"
#include "src/trace_processor/storage/trace_storage.h"
#include "src/trace_processor/tables/profiler_tables_py.h"
#include "src/trace_processor/types/trace_processor_context.h"
#include "src/trace_processor/util/build_id.h"

#include "protos/third_party/pprof/profile.pbzero.h"

namespace perfetto::third_party::perftools::profiles::pbzero {
using Profile = ::perfetto::third_party::perftools::profiles::pbzero::Profile;
using Sample = ::perfetto::third_party::perftools::profiles::pbzero::Sample;
using Location = ::perfetto::third_party::perftools::profiles::pbzero::Location;
using Function = ::perfetto::third_party::perftools::profiles::pbzero::Function;
using Mapping = ::perfetto::third_party::perftools::profiles::pbzero::Mapping;
using Line = ::perfetto::third_party::perftools::profiles::pbzero::Line;
using ValueType =
    ::perfetto::third_party::perftools::profiles::pbzero::ValueType;
}  // namespace perfetto::third_party::perftools::profiles::pbzero

namespace perfetto::trace_processor {

struct FunctionInfo {
  StringId name;
  StringId filename;
  int64_t start_line;
};

PprofTraceReader::PprofTraceReader(TraceProcessorContext* context)
    : context_(context),
      unknown_string_id_(context->storage->InternString("[unknown]")),
      unknown_no_brackets_string_id_(context->storage->InternString("unknown")),
      count_string_id_(context->storage->InternString("count")),
      pprof_file_string_id_(context->storage->InternString("pprof_file")) {}

PprofTraceReader::~PprofTraceReader() = default;

base::Status PprofTraceReader::Parse(TraceBlobView blob) {
  buffer_.insert(buffer_.end(), blob.data(), blob.data() + blob.size());
  return base::OkStatus();
}

base::Status PprofTraceReader::NotifyEndOfFile() {
  if (buffer_.empty()) {
    return base::ErrStatus("Empty pprof data");
  }

  return ParseProfile();
}

base::Status PprofTraceReader::ParseProfile() {
  using namespace perfetto::third_party::perftools::profiles::pbzero;

  TraceStorage* storage = context_->storage.get();
  const Profile::Decoder profile(buffer_.data(), buffer_.size());

  // Parse string table first
  std::vector<StringId> string_table;
  for (auto it = profile.string_table(); it; ++it) {
    string_table.emplace_back(storage->InternString(it->as_string()));
  }

  if (string_table.empty()) {
    return base::ErrStatus("Invalid pprof: empty string table");
  }

  const auto lookup_string_id = [&](int64_t index) -> StringId {
    return index >= 0 && static_cast<size_t>(index) < string_table.size()
               ? string_table[static_cast<size_t>(index)]
               : kNullStringId;
  };

  // Parse mappings and create VirtualMemoryMapping objects
  base::FlatHashMap<uint64_t, VirtualMemoryMapping*> mappings;
  for (auto it = profile.mapping(); it; ++it) {
    const Mapping::Decoder mapping_decoder(*it);
    if (!mapping_decoder.has_id()) {
      continue;
    }

    StringId filename_id = mapping_decoder.has_filename()
                               ? lookup_string_id(mapping_decoder.filename())
                               : unknown_string_id_;
    StringId build_id_id = mapping_decoder.has_build_id()
                               ? lookup_string_id(mapping_decoder.build_id())
                               : kNullStringId;

    CreateMappingParams params;
    params.memory_range = AddressRange::FromStartAndSize(
        mapping_decoder.memory_start(),
        mapping_decoder.memory_limit() - mapping_decoder.memory_start());
    params.exact_offset = mapping_decoder.file_offset();
    params.name = storage->GetString(filename_id).ToStdString();
    if (build_id_id != kNullStringId) {
      params.build_id =
          BuildId::FromRaw(storage->GetString(build_id_id).ToStdString());
    }

    VirtualMemoryMapping& mapping =
        context_->mapping_tracker->InternMemoryMapping(params);
    mappings[mapping_decoder.id()] = &mapping;
  }

  // Parse functions with source file and line information
  base::FlatHashMap<uint64_t, FunctionInfo> functions;
  for (auto it = profile.function(); it; ++it) {
    const Function::Decoder func_decoder(*it);
    if (!func_decoder.has_id()) {
      continue;
    }

    functions[func_decoder.id()] = {
        func_decoder.has_name() ? lookup_string_id(func_decoder.name())
                                : unknown_string_id_,
        func_decoder.has_filename() ? lookup_string_id(func_decoder.filename())
                                    : kNullStringId,
        func_decoder.has_start_line() ? func_decoder.start_line() : 0};
  }

  // Parse locations and create frames
  base::FlatHashMap<uint64_t, FrameId> location_to_frame;
  for (auto it = profile.location(); it; ++it) {
    const Location::Decoder loc_decoder(*it);
    if (!loc_decoder.has_id()) {
      continue;
    }

    // Find or create mapping
    VirtualMemoryMapping* mapping = nullptr;
    if (loc_decoder.has_mapping_id()) {
      if (const auto* found = mappings.Find(loc_decoder.mapping_id())) {
        mapping = *found;
      }
    }
    if (!mapping) {
      mapping = &context_->mapping_tracker->CreateDummyMapping("[unknown]");
    }

    // Extract function information from the first line for frame name
    StringId frame_name_id = unknown_string_id_;
    for (auto line_it = loc_decoder.line(); line_it; ++line_it) {
      const Line::Decoder line_decoder(*line_it);
      if (!line_decoder.has_function_id()) {
        continue;
      }
      const auto* func_info = functions.Find(line_decoder.function_id());
      if (func_info) {
        frame_name_id = func_info->name;
        break;
      }
    }

    // Calculate relative PC
    uint64_t rel_pc = 0;
    if (loc_decoder.has_address()) {
      rel_pc = loc_decoder.address();
      uint64_t mapping_start = mapping->memory_range().start();
      if (mapping_start > 0 && rel_pc >= mapping_start) {
        rel_pc -= mapping_start;
      }
    }

    // Create frame
    FrameId frame_id =
        mapping->InternFrame(rel_pc, storage->GetString(frame_name_id));
    location_to_frame[loc_decoder.id()] = frame_id;

    // Create symbol table entries for all line entries (inlined functions)
    uint32_t symbol_set_id = storage->symbol_table().row_count();
    bool has_symbols = false;

    // First collect valid line data to determine inlining status
    struct LineData {
      uint64_t function_id;
      int64_t line;
    };
    std::vector<LineData> valid_lines;
    for (auto line_it = loc_decoder.line(); line_it; ++line_it) {
      const Line::Decoder line_decoder(*line_it);
      if (!line_decoder.has_function_id()) {
        continue;
      }
      const auto* func_info = functions.Find(line_decoder.function_id());
      if (func_info) {
        valid_lines.push_back(
            {line_decoder.function_id(),
             line_decoder.has_line() ? line_decoder.line() : 0});
      }
    }

    // Create symbol entries with inlined flag
    for (size_t i = 0; i < valid_lines.size(); ++i) {
      const auto& line_data = valid_lines[i];
      const auto* func_info = functions.Find(line_data.function_id);

      StringId function_name_id = func_info->name;
      StringId source_file_id = func_info->filename;

      // Use line number from Line if available, otherwise use function's
      // start_line
      int64_t line_number = 0;
      if (line_data.line > 0) {
        line_number = line_data.line;
      } else if (func_info->start_line > 0) {
        line_number = func_info->start_line;
      }

      std::optional<uint32_t> line_opt =
          line_number > 0
              ? std::make_optional(static_cast<uint32_t>(line_number))
              : std::nullopt;

      // Determine if this function is inlined
      // All functions except the last one (outermost) are inlined
      // If there's only one function, it's not inlined
      bool is_inlined = valid_lines.size() > 1 && i < valid_lines.size() - 1;

      // Insert symbol with source file, line number, and inlined flag
      storage->mutable_symbol_table()->Insert({
          symbol_set_id,
          function_name_id,
          source_file_id,
          line_opt,
          is_inlined,
      });
      has_symbols = true;
    }

    // Link the frame to the symbol set if we created any symbols
    if (has_symbols) {
      auto* frames = storage->mutable_stack_profile_frame_table();
      auto frame_row = *frames->FindById(frame_id);
      frame_row.set_symbol_set_id(symbol_set_id);
    }
  }

  // Parse sample types and create aggregate_profile entries
  std::vector<tables::AggregateProfileTable::Id> profile_ids;
  for (auto it = profile.sample_type(); it; ++it) {
    const ValueType::Decoder sample_type_decoder(*it);

    StringId type_str_id = sample_type_decoder.has_type()
                               ? lookup_string_id(sample_type_decoder.type())
                               : unknown_no_brackets_string_id_;
    StringId unit_str_id = sample_type_decoder.has_unit()
                               ? lookup_string_id(sample_type_decoder.unit())
                               : count_string_id_;

    std::string type_str = storage->GetString(type_str_id).ToStdString();
    auto profile_id =
        storage->mutable_aggregate_profile_table()
            ->Insert({pprof_file_string_id_,
                      storage->InternString(("pprof " + type_str).c_str()),
                      type_str_id, unit_str_id})
            .id;
    profile_ids.push_back(profile_id);
  }

  // Parse samples and create aggregate_sample entries
  for (auto it = profile.sample(); it; ++it) {
    const Sample::Decoder sample_decoder(*it);

    // Materialize location_ids first (pprof format: leaf is at [0])
    std::vector<uint64_t> location_ids;
    bool location_parse_error = false;
    auto loc_it = sample_decoder.GetUnifiedRepeated<
        protozero::proto_utils::ProtoWireType::kVarInt, uint64_t>(
        Sample::kLocationIdFieldNumber, &location_parse_error);
    for (; loc_it && !location_parse_error; ++loc_it) {
      location_ids.push_back(*loc_it);
    }

    if (location_ids.empty()) {
      continue;
    }

    // Reverse to get root -> leaf order for callsite building
    std::reverse(location_ids.begin(), location_ids.end());

    // Build callsite hierarchy from root to leaf
    std::optional<CallsiteId> callsite_id;
    uint32_t depth = 0;
    for (uint64_t location_id : location_ids) {
      const auto* frame_id = location_to_frame.Find(location_id);
      if (!frame_id) {
        continue;
      }

      callsite_id = context_->stack_profile_tracker->InternCallsite(
          callsite_id, *frame_id, depth);
      ++depth;
    }

    if (!callsite_id) {
      continue;
    }

    // Create aggregate_sample entries for each value
    size_t value_index = 0;
    bool values_parse_error = false;
    auto value_it = sample_decoder.GetUnifiedRepeated<
        protozero::proto_utils::ProtoWireType::kVarInt, int64_t>(
        Sample::kValueFieldNumber, &values_parse_error);
    for (; !values_parse_error && value_it && value_index < profile_ids.size();
         ++value_it, ++value_index) {
      // Cast to double as aggregate_sample table expects double values
      // while pprof stores values as int64_t
      storage->mutable_aggregate_sample_table()->Insert(
          {profile_ids[value_index], *callsite_id,
           static_cast<double>(*value_it)});
    }
  }
  return base::OkStatus();
}

}  // namespace perfetto::trace_processor
