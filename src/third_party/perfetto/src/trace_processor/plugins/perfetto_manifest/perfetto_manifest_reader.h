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

#ifndef SRC_TRACE_PROCESSOR_PLUGINS_PERFETTO_MANIFEST_PERFETTO_MANIFEST_READER_H_
#define SRC_TRACE_PROCESSOR_PLUGINS_PERFETTO_MANIFEST_PERFETTO_MANIFEST_READER_H_

#include <cstdint>
#include <memory>
#include <string>

#include "perfetto/base/status.h"
#include "perfetto/trace_processor/trace_blob_view.h"
#include "src/trace_processor/importers/common/chunked_trace_reader.h"

namespace perfetto::trace_processor {
class TraceProcessorContext;
class TraceImporterBase;

// Creates the importer for the perfetto_manifest sidecar file.
std::unique_ptr<TraceImporterBase> CreatePerfettoManifestImporter();
}  // namespace perfetto::trace_processor

namespace perfetto::trace_processor::perfetto_manifest {

// Reads a perfetto_manifest sidecar file: a JSON file which, as the first
// file of the trace, overrides clock and machine handling for the files
// that follow. The parsed configuration is stored on the global
// TraceManifestState and consulted by ForwardingTraceParser for each trace
// file.
class PerfettoManifestReader : public ChunkedTraceReader {
 public:
  // `file_id` is this file's trace_file_table id, used as the owner when the
  // file claims the trace time clock; it is unique, so no later trace file
  // can override the choice.
  PerfettoManifestReader(TraceProcessorContext* context, uint32_t file_id);
  ~PerfettoManifestReader() override;

  // ChunkedTraceReader implementation.
  base::Status Parse(TraceBlobView) override;
  base::Status OnPushDataToSorter() override;
  void OnEventsFullyExtracted() override {}

 private:
  // Applies the parsed manifest (machine rows, trace-time clock, clock-override
  // edges) to the clock graph. Called by OnPushDataToSorter after parsing.
  base::Status ApplyManifest();

  TraceProcessorContext* const context_;
  const uint32_t file_id_;
  std::string buffer_;
};

}  // namespace perfetto::trace_processor::perfetto_manifest

#endif  // SRC_TRACE_PROCESSOR_PLUGINS_PERFETTO_MANIFEST_PERFETTO_MANIFEST_READER_H_
