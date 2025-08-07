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

#include "src/trace_processor/importers/perf_text/perf_text_trace_tokenizer.h"

#include <cctype>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "perfetto/base/logging.h"
#include "perfetto/base/status.h"
#include "perfetto/ext/base/string_utils.h"
#include "perfetto/ext/base/string_view.h"
#include "perfetto/trace_processor/trace_blob_view.h"
#include "src/trace_processor/importers/common/mapping_tracker.h"
#include "src/trace_processor/importers/common/stack_profile_tracker.h"
#include "src/trace_processor/importers/common/virtual_memory_mapping.h"
#include "src/trace_processor/importers/perf_text/perf_text_event.h"
#include "src/trace_processor/importers/perf_text/perf_text_sample_line_parser.h"
#include "src/trace_processor/sorter/trace_sorter.h"
#include "src/trace_processor/storage/stats.h"
#include "src/trace_processor/storage/trace_storage.h"
#include "src/trace_processor/types/trace_processor_context.h"
#include "src/trace_processor/util/trace_blob_view_reader.h"

namespace perfetto::trace_processor::perf_text_importer {

namespace {

std::string_view ToStringView(const TraceBlobView& tbv) {
  return {reinterpret_cast<const char*>(tbv.data()), tbv.size()};
}

std::string Slice(const std::string& str, size_t start, size_t end) {
  return str.substr(start, end - start);
}

}  // namespace

PerfTextTraceTokenizer::PerfTextTraceTokenizer(TraceProcessorContext* ctx)
    : context_(ctx) {}
PerfTextTraceTokenizer::~PerfTextTraceTokenizer() = default;

base::Status PerfTextTraceTokenizer::Parse(TraceBlobView blob) {
  reader_.PushBack(std::move(blob));
  std::vector<FrameId> frames;
  // Loop over each sample.
  for (;;) {
    auto it = reader_.GetIterator();
    auto r = it.MaybeFindAndRead('\n');
    if (!r) {
      return base::OkStatus();
    }
    // The start line of a sample. An example:
    // trace_processor 3962131 303057.417513:          1 cpu_atom/cycles/Pu:
    //
    // Note that perf script output is fully configurable so we have to be
    // parse all the optionality carefully.
    std::string_view first_line = ToStringView(*r);
    std::optional<SampleLine> sample = ParseSampleLine(first_line);
    if (!sample) {
      return base::ErrStatus(
          "Perf text parser: unable to parse sample line (context: '%s')",
          std::string(first_line).c_str());
    }

    // Loop over the frames in the sample.
    for (;;) {
      auto raw_frame = it.MaybeFindAndRead('\n');
      // If we don't manage to parse the full stack, we should bail out.
      if (!raw_frame) {
        return base::OkStatus();
      }
      // An empty line indicates that we have reached the end of this sample.
      std::string frame =
          base::TrimWhitespace(std::string(ToStringView(*raw_frame)));
      if (frame.size() == 0) {
        break;
      }

      size_t symbol_end = frame.find(' ');
      if (symbol_end == std::string::npos) {
        return base::ErrStatus(
            "Perf text parser: unable to find symbol in frame (context: '%s')",
            frame.c_str());
      }

      size_t mapping_start = frame.rfind('(');
      if (mapping_start == std::string::npos || frame.back() != ')') {
        return base::ErrStatus(
            "Perf text parser: unable to find mapping in frame (context: '%s')",
            frame.c_str());
      }

      std::string mapping_name =
          Slice(frame, mapping_start + 1, frame.size() - 1);
      DummyMemoryMapping* mapping;
      if (DummyMemoryMapping** mapping_ptr = mappings_.Find(mapping_name);
          mapping_ptr) {
        mapping = *mapping_ptr;
      } else {
        mapping = &context_->mapping_tracker->CreateDummyMapping(mapping_name);
        PERFETTO_CHECK(mappings_.Insert(mapping_name, mapping).second);
      }

      std::string symbol_name_with_offset =
          base::TrimWhitespace(Slice(frame, symbol_end, mapping_start));
      size_t offset = symbol_name_with_offset.rfind('+');
      base::StringView symbol_name(symbol_name_with_offset);
      if (offset != std::string::npos) {
        symbol_name = symbol_name.substr(0, offset);
      }
      frames.emplace_back(
          mapping->InternDummyFrame(symbol_name, base::StringView()));
    }
    if (frames.empty()) {
      context_->storage->IncrementStats(
          stats::perf_text_importer_sample_no_frames);
      continue;
    }

    std::optional<CallsiteId> parent_callsite;
    uint32_t depth = 0;
    for (auto rit = frames.rbegin(); rit != frames.rend(); ++rit) {
      parent_callsite = context_->stack_profile_tracker->InternCallsite(
          parent_callsite, *rit, ++depth);
    }
    frames.clear();

    PerfTextEvent evt;
    if (!sample->comm.empty()) {
      evt.comm = context_->storage->InternString(
          base::StringView(sample->comm.data(), sample->comm.size()));
    }
    evt.tid = sample->tid;
    evt.pid = sample->pid;
    evt.callsite_id = *parent_callsite;

    context_->sorter->PushPerfTextEvent(sample->ts, evt);
    reader_.PopFrontUntil(it.file_offset());
  }
}

base::Status PerfTextTraceTokenizer::NotifyEndOfFile() {
  return base::OkStatus();
}

}  // namespace perfetto::trace_processor::perf_text_importer
