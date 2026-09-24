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

#ifndef SRC_TRACE_PROCESSOR_TRACE_READER_REGISTRY_H_
#define SRC_TRACE_PROCESSOR_TRACE_READER_REGISTRY_H_

#include <cstdint>
#include <memory>

#include "perfetto/ext/base/status_or.h"
#include "src/trace_processor/util/trace_type.h"

namespace perfetto::trace_processor {

class ChunkedTraceReader;
class TraceProcessorContext;

// Owns the trace importer registry and creates readers from it. A thin layer
// over TraceImporterRegistry that lives above the util layer so it can build
// the (incomplete-in-util) ChunkedTraceReader.
class TraceReaderRegistry {
 public:
  TraceReaderRegistry() = default;

  // Registers an importer (builtin or plugin) keyed by its identity. A trace
  // reader and its importer are 1:1, so this is the sole registration point.
  // Returns the assigned id (rarely needed).
  TraceImporterId Register(std::unique_ptr<TraceImporterBase> importer) {
    return importers_.Register(std::move(importer));
  }

  // The registry of trace importers, owned here. Exposed as a pointer so
  // TraceProcessorContext can publish it to low-layer code that needs per-type
  // metadata without depending on this header.
  TraceImporterRegistry* importer_registry() { return &importers_; }

  // Creates a new `ChunkedTraceReader` for `id`, `file_id` being the
  // trace_file_table id of the file being read. Returns an error if `id` is
  // not registered.
  base::StatusOr<std::unique_ptr<ChunkedTraceReader>> CreateTraceReader(
      TraceImporterId id,
      TraceProcessorContext* context,
      uint32_t file_id);

 private:
  TraceImporterRegistry importers_;
};

}  // namespace perfetto::trace_processor

#endif  // SRC_TRACE_PROCESSOR_TRACE_READER_REGISTRY_H_
