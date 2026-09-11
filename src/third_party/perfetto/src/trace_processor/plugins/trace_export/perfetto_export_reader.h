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

#ifndef SRC_TRACE_PROCESSOR_PLUGINS_TRACE_EXPORT_PERFETTO_EXPORT_READER_H_
#define SRC_TRACE_PROCESSOR_PLUGINS_TRACE_EXPORT_PERFETTO_EXPORT_READER_H_

#include <cstdint>
#include <memory>
#include <vector>

#include "perfetto/base/status.h"
#include "perfetto/trace_processor/trace_blob_view.h"
#include "src/trace_processor/core/plugin/registration.h"
#include "src/trace_processor/importers/common/chunked_trace_reader.h"
#include "src/trace_processor/plugins/trace_export/perfetto_export_manifest.h"
#include "src/trace_processor/util/trace_blob_view_reader.h"

namespace perfetto::trace_processor {
class TraceProcessorContext;
class TraceImporterBase;
}  // namespace perfetto::trace_processor

namespace perfetto::trace_processor::trace_export {

struct PerfettoExportPluginState {
  const std::vector<PluginDataframe>* dataframes = nullptr;
  bool validated = false;
  std::vector<ResolvedPerfettoExportTable> tables;
  std::vector<bool> seen;
};

// Creates the importer for Arrow members declared by a kPerfetto archive's
// perfetto_manifest. Standalone Arrow files and kArrowTar archives are for
// external consumers and cannot be loaded as input traces.
std::unique_ptr<TraceImporterBase> CreatePerfettoExportTableImporter(
    PerfettoExportPluginState* state);

class PerfettoExportTableReader : public ChunkedTraceReader {
 public:
  PerfettoExportTableReader(TraceProcessorContext* context,
                            PerfettoExportPluginState* state,
                            uint32_t file_id);
  ~PerfettoExportTableReader() override;

  base::Status Parse(TraceBlobView) override;
  base::Status OnPushDataToSorter() override;
  void OnEventsFullyExtracted() override {}

 private:
  TraceProcessorContext* const context_;
  PerfettoExportPluginState* const state_;
  const uint32_t file_id_;
  util::TraceBlobViewReader buffer_;
};

}  // namespace perfetto::trace_processor::trace_export

#endif  // SRC_TRACE_PROCESSOR_PLUGINS_TRACE_EXPORT_PERFETTO_EXPORT_READER_H_
