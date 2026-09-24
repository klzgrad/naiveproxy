/*
 * Copyright (C) 2019 The Android Open Source Project
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

#include "src/traceconv/trace_to_json.h"

#include <stdio.h>

#include "perfetto/base/logging.h"
#include "perfetto/ext/trace_processor/export_json.h"
#include "perfetto/trace_processor/trace_processor.h"
#include "src/traceconv/utils.h"

namespace perfetto {
namespace trace_to_text {

namespace {

const char kTraceFooter[] = R"(,
  "controllerTraceDataKey": "systraceController"
})";

class TraceWriterOutputWriter final
    : public trace_processor::json::OutputWriter {
 public:
  explicit TraceWriterOutputWriter(TraceWriter* writer) : writer_(writer) {}

  base::Status AppendString(const std::string& value) override {
    if (value.empty()) {
      return base::OkStatus();
    }
    if (has_trailing_byte_) {
      writer_->Write(&trailing_byte_, 1);
    }
    writer_->Write(value.data(), value.size() - 1);
    trailing_byte_ = value.back();
    has_trailing_byte_ = true;
    return base::OkStatus();
  }

  bool HasTrailingBrace() const {
    return has_trailing_byte_ && trailing_byte_ == '}';
  }

 private:
  TraceWriter* writer_;
  char trailing_byte_ = 0;
  bool has_trailing_byte_ = false;
};

bool ExportUserspaceEvents(trace_processor::TraceProcessor* tp,
                           TraceWriter* writer) {
  ProgressLine("Converting userspace events");

  TraceWriterOutputWriter output(writer);
  base::Status status = trace_processor::json::ExportJson(tp, &output);
  EndProgressLine();
  if (!status.ok()) {
    PERFETTO_ELOG("Could not convert userspace events: %s", status.c_message());
    return false;
  }
  if (!output.HasTrailingBrace()) {
    PERFETTO_ELOG("Could not convert userspace events: invalid JSON output");
    return false;
  }
  return true;
}

}  // namespace

base::Status TraceToJson(std::istream* input,
                         std::ostream* output,
                         bool compress,
                         Keep truncate_keep,
                         bool full_sort) {
  std::unique_ptr<TraceWriter> trace_writer(
      compress ? new DeflateTraceWriter(output) : new TraceWriter(output));

  trace_processor::Config config;
  config.sorting_mode = full_sort
                            ? trace_processor::SortingMode::kForceFullSort
                            : trace_processor::SortingMode::kDefaultHeuristics;
  std::unique_ptr<trace_processor::TraceProcessor> tp =
      trace_processor::TraceProcessor::CreateInstance(config);

  if (!ReadTraceUnfinalized(tp.get(), input))
    return base::ErrStatus("failed to read trace");
  if (auto status = tp->NotifyEndOfFile(); !status.ok()) {
    return base::ErrStatus("failed to finalize trace: %s", status.c_message());
  }

  // TODO(eseckler): Support truncation of userspace event data.
  if (!ExportUserspaceEvents(tp.get(), trace_writer.get())) {
    // ExportJson streams directly to |trace_writer|, so emitting an empty
    // trace header here would corrupt any output already written. Report the
    // conversion failure instead of silently dropping userspace events.
    return base::ErrStatus(
        "failed to convert userspace events (see errors above)");
  }
  trace_writer->Write(",\n");

  int ret = ExtractSystrace(tp.get(), trace_writer.get(),
                            /*wrapped_in_json=*/true, truncate_keep);
  if (ret) {
    EndProgressLine();
    return base::ErrStatus("failed to convert ftrace events");
  }

  trace_writer->Write(kTraceFooter);
  EndProgressLine();
  return base::OkStatus();
}

}  // namespace trace_to_text
}  // namespace perfetto
