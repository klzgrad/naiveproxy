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

#include "src/trace_processor/plugins/strace/strace.h"

#include <cstddef>
#include <cstdint>
#include <memory>

#include "perfetto/base/compiler.h"
#include "src/trace_processor/core/plugin/plugin.h"
#include "src/trace_processor/importers/common/chunked_trace_reader.h"
#include "src/trace_processor/plugins/strace/strace_trace_tokenizer.h"
#include "src/trace_processor/trace_reader_registry.h"
#include "src/trace_processor/types/trace_processor_context.h"
#include "src/trace_processor/util/trace_type.h"

namespace perfetto::trace_processor::strace_importer {
namespace {

// The strace trace type: the textual output of `strace -ttt`.
class StraceImporter : public TraceImporter<StraceImporter> {
 public:
  StraceImporter() : TraceImporter(MakeDescriptor()) {}

  bool Sniff(const uint8_t* data, size_t size) const override {
    return IsStraceFormatTrace(data, size);
  }

  base::StatusOr<std::unique_ptr<ChunkedTraceReader>> CreateReader(
      TraceProcessorContext* context,
      uint32_t) const override {
    return std::unique_ptr<ChunkedTraceReader>(
        std::make_unique<StraceTraceTokenizer>(context));
  }

 private:
  static TraceTypeDescriptor MakeDescriptor() {
    TraceTypeDescriptor d;
    d.name = "strace";
    // Only `-ttt` (Unix epoch seconds, with an optional fractional part) is
    // accepted as input, so every timestamp we emit genuinely is an
    // absolute realtime moment; treated as BUILTIN_CLOCK_REALTIME, same as
    // android_logcat/android_dumpstate. `-t`/`-tt` (wall-clock
    // time-of-day, no date) lines are rejected by the parser rather than
    // misinterpreted as realtime — see strace_trace_tokenizer.h.
    d.clock_policy = TraceClockPolicy::kRealtime;
    // Must run before the generic systrace fallback (200), which otherwise
    // claims any input starting with a leading space.
    d.detection_priority = 197;
    return d;
  }
};

class StracePlugin : public Plugin<StracePlugin> {
 public:
  ~StracePlugin() override;

  void RegisterImporters(TraceReaderRegistry& registry) override {
    registry.Register(std::make_unique<StraceImporter>());
  }
};

StracePlugin::~StracePlugin() = default;

}  // namespace

void RegisterPlugin() {
  static PluginRegistration reg(
      []() -> std::unique_ptr<PluginBase> {
        return std::make_unique<StracePlugin>();
      },
      StracePlugin::kPluginId, StracePlugin::kDepIds.data(),
      StracePlugin::kDepIds.size());
  base::ignore_result(reg);
}

}  // namespace perfetto::trace_processor::strace_importer
