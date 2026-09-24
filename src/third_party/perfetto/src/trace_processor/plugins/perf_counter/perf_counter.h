// Copyright (C) 2026 The Android Open Source Project
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//      http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#ifndef SRC_TRACE_PROCESSOR_PLUGINS_PERF_COUNTER_PERF_COUNTER_H_
#define SRC_TRACE_PROCESSOR_PLUGINS_PERF_COUNTER_PERF_COUNTER_H_

#include <memory>

#include "src/trace_processor/sqlite/bindings/sqlite_function.h"

namespace perfetto::trace_processor {

class TraceStorage;
class PerfCounterExtractor;  // Defined in .cc file

// __intrinsic_perf_counter_for_sample(sample_id, counter_name)
// Returns the counter value for a given sample and counter name.
struct PerfCounterForSampleFunction
    : public sqlite::Function<PerfCounterForSampleFunction> {
  static constexpr char kName[] = "__intrinsic_perf_counter_for_sample";
  static constexpr int kArgCount = 2;

  struct Context {
    Context(TraceStorage* s);
    ~Context();

    TraceStorage* storage;
    std::unique_ptr<PerfCounterExtractor> extractor;
  };

  using UserData = Context;

  static void Step(sqlite3_context* ctx, int, sqlite3_value** argv);
};

}  // namespace perfetto::trace_processor

namespace perfetto::trace_processor::perf_counter {

// Registers the PerfCounter plugin with the global plugin set. Idempotent;
// only the first call has an effect. Must run before the first GetPluginSet()
// call (i.e. before constructing TraceProcessorImpl).
void RegisterPlugin();

}  // namespace perfetto::trace_processor::perf_counter

#endif  // SRC_TRACE_PROCESSOR_PLUGINS_PERF_COUNTER_PERF_COUNTER_H_
