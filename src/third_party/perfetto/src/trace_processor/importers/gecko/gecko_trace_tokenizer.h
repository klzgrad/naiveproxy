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

#ifndef SRC_TRACE_PROCESSOR_IMPORTERS_GECKO_GECKO_TRACE_TOKENIZER_H_
#define SRC_TRACE_PROCESSOR_IMPORTERS_GECKO_GECKO_TRACE_TOKENIZER_H_

#include <memory>
#include <string>
#include <vector>

#include "perfetto/base/status.h"
#include "perfetto/ext/base/flat_hash_map.h"
#include "src/trace_processor/importers/common/chunked_trace_reader.h"
#include "src/trace_processor/importers/common/clock_tracker.h"
#include "src/trace_processor/importers/gecko/gecko_event.h"
#include "src/trace_processor/sorter/trace_sorter.h"
#include "src/trace_processor/storage/trace_storage.h"
#include "src/trace_processor/types/trace_processor_context.h"

namespace perfetto::trace_processor {
class DummyMemoryMapping;
}

namespace perfetto::trace_processor::gecko_importer {

// Forward declaration for internal struct.
struct GeckoThread;

struct Callsite {
  CallsiteId id;
  uint32_t depth;
};

class GeckoTraceTokenizer : public ChunkedTraceReader {
 public:
  explicit GeckoTraceTokenizer(TraceProcessorContext*);
  ~GeckoTraceTokenizer() override;

  base::Status Parse(TraceBlobView) override;
  base::Status OnPushDataToSorter() override;
  void OnEventsFullyExtracted() override {}

 private:
  // Processes frames and stacks, returning callsites. `strings` is the string
  // table to use for resolving frame names; for traces with a `shared` block
  // that owns the string table this is `shared.strings` rather than
  // `t.strings`.
  std::vector<Callsite> ProcessPreprocessedFramesAndStacks(
      const GeckoThread& t,
      const std::vector<std::string>& strings);
  std::vector<Callsite> ProcessLegacyFramesAndStacks(
      const GeckoThread& t,
      const std::vector<std::string>& strings);

  // Processes samples using pre-built callsites.
  void ProcessSamples(const GeckoThread& t,
                      const std::vector<Callsite>& callsites);
  void ProcessLegacySamples(const GeckoThread& t,
                            const std::vector<Callsite>& callsites);

  // Emits a MarkerEvent for each entry in `t.markers`. `strings_for_names`
  // resolves the marker name index (which may live in a shared string table
  // when the file uses the modern `shared` block).
  void ProcessMarkers(const GeckoThread& t,
                      const std::vector<std::string>& strings_for_names,
                      const std::vector<std::string>& category_names);

  FrameId InternFrame(base::StringView name);

  TraceProcessorContext* const context_;
  std::unique_ptr<TraceSorter::Stream<GeckoEvent>> stream_;
  std::string pending_json_;
  ClockTracker::ClockId trace_file_clock_;

  // Shared across all threads to avoid creating duplicate mappings.
  DummyMemoryMapping* dummy_mapping_ = nullptr;
  base::FlatHashMap<std::string, DummyMemoryMapping*> mappings_;
};

}  // namespace perfetto::trace_processor::gecko_importer

#endif  // SRC_TRACE_PROCESSOR_IMPORTERS_GECKO_GECKO_TRACE_TOKENIZER_H_
