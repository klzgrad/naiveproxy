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

#include "src/trace_processor/plugins/perf_text/perf_text.h"

#include <cstddef>
#include <cstdint>
#include <memory>

#include "perfetto/base/compiler.h"
#include "src/trace_processor/core/plugin/plugin.h"
#include "src/trace_processor/importers/common/chunked_trace_reader.h"
#include "src/trace_processor/plugins/perf_text/perf_text_sample_line_parser.h"
#include "src/trace_processor/plugins/perf_text/perf_text_trace_tokenizer.h"
#include "src/trace_processor/trace_reader_registry.h"
#include "src/trace_processor/types/trace_processor_context.h"
#include "src/trace_processor/util/trace_type.h"

namespace perfetto::trace_processor::perf_text_importer {
namespace {

// The perf_text trace type: the textual output of `perf script` and the
// equivalent simpleperf command.
class PerfTextImporter : public TraceImporter<PerfTextImporter> {
 public:
  PerfTextImporter() : TraceImporter(MakeDescriptor()) {}

  bool Sniff(const uint8_t* data, size_t size) const override {
    return IsPerfTextFormatTrace(data, size);
  }

  base::StatusOr<std::unique_ptr<ChunkedTraceReader>> CreateReader(
      TraceProcessorContext* context,
      uint32_t) const override {
    return std::unique_ptr<ChunkedTraceReader>(
        std::make_unique<PerfTextTraceTokenizer>(context));
  }

 private:
  static TraceTypeDescriptor MakeDescriptor() {
    TraceTypeDescriptor d;
    d.name = "perf_text";
    d.clock_policy = TraceClockPolicy::kMonotonic;
    d.detection_priority = 195;
    return d;
  }
};

class PerfTextPlugin : public Plugin<PerfTextPlugin> {
 public:
  ~PerfTextPlugin() override;

  void RegisterImporters(TraceReaderRegistry& registry) override {
    registry.Register(std::make_unique<PerfTextImporter>());
  }
};

PerfTextPlugin::~PerfTextPlugin() = default;

}  // namespace

void RegisterPlugin() {
  static PluginRegistration reg(
      []() -> std::unique_ptr<PluginBase> {
        return std::make_unique<PerfTextPlugin>();
      },
      PerfTextPlugin::kPluginId, PerfTextPlugin::kDepIds.data(),
      PerfTextPlugin::kDepIds.size());
  base::ignore_result(reg);
}

}  // namespace perfetto::trace_processor::perf_text_importer
