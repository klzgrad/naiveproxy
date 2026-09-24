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

#include "src/trace_processor/importers/collapsed_stack/collapsed_stack_trace_reader.h"

#include <cctype>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "perfetto/base/status.h"
#include "perfetto/ext/base/status_macros.h"
#include "perfetto/ext/base/string_utils.h"
#include "perfetto/ext/base/string_view.h"
#include "perfetto/trace_processor/trace_blob_view.h"
#include "src/trace_processor/importers/common/builtin_trace_importers.h"
#include "src/trace_processor/importers/common/mapping_tracker.h"
#include "src/trace_processor/importers/common/stack_profile_tracker.h"
#include "src/trace_processor/storage/trace_storage.h"
#include "src/trace_processor/types/trace_processor_context.h"
#include "src/trace_processor/util/trace_type.h"

namespace perfetto::trace_processor {

namespace {

std::string_view ToStringView(const TraceBlobView& tbv) {
  return {reinterpret_cast<const char*>(tbv.data()), tbv.size()};
}

}  // namespace

CollapsedStackTraceReader::CollapsedStackTraceReader(
    TraceProcessorContext* context)
    : context_(context) {}

CollapsedStackTraceReader::~CollapsedStackTraceReader() = default;

base::Status CollapsedStackTraceReader::Parse(TraceBlobView blob) {
  reader_.PushBack(std::move(blob));

  for (auto it = reader_.GetIterator(); it;) {
    std::optional<TraceBlobView> line_tbv = it.MaybeFindAndRead('\n');
    if (!line_tbv) {
      // Incomplete line - wait for more data.
      break;
    }

    std::string_view line = ToStringView(*line_tbv);
    RETURN_IF_ERROR(ParseLine(line));
    reader_.PopFrontUntil(it.file_offset());
  }

  return base::OkStatus();
}

base::Status CollapsedStackTraceReader::OnPushDataToSorter() {
  // Process any remaining data without a trailing newline.
  if (!reader_.empty()) {
    std::optional<TraceBlobView> remaining =
        reader_.SliceOff(reader_.start_offset(), reader_.avail());
    if (remaining && remaining->size() > 0) {
      std::string_view line = ToStringView(*remaining);
      RETURN_IF_ERROR(ParseLine(line));
    }
  }
  return base::OkStatus();
}

base::Status CollapsedStackTraceReader::ParseLine(std::string_view line) {
  // Trim whitespace.
  std::string trimmed = base::TrimWhitespace(std::string(line));
  if (trimmed.empty() || trimmed[0] == '#') {
    return base::OkStatus();
  }

  // Lazily initialize the profile and mapping on first valid line.
  if (!profile_id_) {
    TraceStorage* storage = context_->storage.get();
    StringId scope = storage->InternString("collapsed_stack_file");
    StringId name = storage->InternString("collapsed_stack samples");
    StringId type = storage->InternString("samples");
    StringId unit = storage->InternString("count");
    profile_id_ = storage->mutable_aggregate_profile_table()
                      ->Insert({scope, name, type, unit})
                      .id;
    mapping_ =
        &context_->mapping_tracker->CreateDummyMapping("[collapsed_stack]");
  }

  // Find the last space to separate stack from count.
  size_t last_space = trimmed.rfind(' ');
  if (last_space == std::string::npos || last_space == 0) {
    return base::OkStatus();  // Malformed line, skip.
  }

  std::string_view stack_str(trimmed.data(), last_space);
  std::string_view count_str(trimmed.data() + last_space + 1,
                             trimmed.size() - last_space - 1);

  std::optional<int64_t> count = base::StringToInt64(std::string(count_str));
  if (!count || *count <= 0) {
    return base::OkStatus();  // Invalid count, skip.
  }

  // Parse the stack frames (semicolon-separated, root first).
  std::vector<std::string> frames =
      base::SplitString(std::string(stack_str), ";");
  if (frames.empty()) {
    return base::OkStatus();
  }

  // Build callsite hierarchy from root to leaf.
  std::optional<CallsiteId> callsite_id;
  uint32_t depth = 0;
  for (const std::string& frame_name : frames) {
    if (frame_name.empty()) {
      continue;
    }
    FrameId frame_id = mapping_->InternDummyFrame(base::StringView(frame_name),
                                                  base::StringView());
    callsite_id = context_->stack_profile_tracker->InternCallsite(
        callsite_id, frame_id, depth);
    ++depth;
  }

  if (callsite_id) {
    context_->storage->mutable_aggregate_sample_table()->Insert(
        {*profile_id_, *callsite_id, static_cast<double>(*count)});
  }

  return base::OkStatus();
}

namespace {

// Checks if a line looks like a valid collapsed stack line:
// frame1;frame2;frame3 count
bool IsCollapsedStackLine(const char* line_start, size_t line_len) {
  size_t start = 0;
  while (start < line_len && base::IsSpace(line_start[start])) {
    ++start;
  }
  if (start >= line_len || line_start[start] == '#') {
    return false;
  }

  size_t end = line_len;
  while (end > start && base::IsSpace(line_start[end - 1])) {
    --end;
  }

  size_t len = end - start;
  if (len == 0) {
    return false;
  }

  const char* line = line_start + start;

  size_t last_space = len;
  for (size_t i = len; i > 0; --i) {
    if (line[i - 1] == ' ') {
      last_space = i - 1;
      break;
    }
  }

  if (last_space == len || last_space == 0) {
    return false;
  }

  for (size_t i = last_space + 1; i < len; ++i) {
    if (!std::isdigit(static_cast<unsigned char>(line[i]))) {
      return false;
    }
  }

  bool has_semicolon = false;
  for (size_t i = 0; i < last_space; ++i) {
    if (line[i] == ';') {
      has_semicolon = true;
      break;
    }
  }
  return has_semicolon;
}

bool IsCollapsedStackFormat(const uint8_t* data, size_t size) {
  const char* str = reinterpret_cast<const char*>(data);
  size_t valid_lines = 0;
  size_t pos = 0;

  while (pos < size && valid_lines < 3) {
    size_t nl = pos;
    while (nl < size && str[nl] != '\n') {
      ++nl;
    }

    size_t line_len = nl - pos;
    size_t start = pos;
    while (start < nl && base::IsSpace(str[start])) {
      ++start;
    }

    if (start < nl && str[start] != '#') {
      if (!IsCollapsedStackLine(str + pos, line_len)) {
        return false;
      }
      ++valid_lines;
    }

    pos = (nl < size) ? nl + 1 : size;
  }

  return valid_lines > 0;
}

// Collapsed stack (flamegraph input) format.
class CollapsedStackImporter : public TraceImporter<CollapsedStackImporter> {
 public:
  CollapsedStackImporter() : TraceImporter(MakeDescriptor()) {}
  ~CollapsedStackImporter() override;

  bool Sniff(const uint8_t* data, size_t size) const override {
    return IsCollapsedStackFormat(data, size);
  }

  base::StatusOr<std::unique_ptr<ChunkedTraceReader>> CreateReader(
      TraceProcessorContext* context,
      uint32_t) const override {
    return std::unique_ptr<ChunkedTraceReader>(
        std::make_unique<CollapsedStackTraceReader>(context));
  }

 private:
  static TraceTypeDescriptor MakeDescriptor() {
    TraceTypeDescriptor d;
    d.name = "collapsed_stack";
    d.detection_priority = 190;
    return d;
  }
};

CollapsedStackImporter::~CollapsedStackImporter() = default;

}  // namespace

std::unique_ptr<TraceImporterBase> CreateCollapsedStackImporter() {
  return std::make_unique<CollapsedStackImporter>();
}

}  // namespace perfetto::trace_processor
