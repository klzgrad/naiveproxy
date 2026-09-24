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

#include "src/trace_processor/trace_reader_registry.h"

#include <memory>

#include "perfetto/base/status.h"
#include "perfetto/ext/base/status_or.h"
#include "src/trace_processor/importers/common/chunked_trace_reader.h"
#include "src/trace_processor/util/trace_type.h"

namespace perfetto::trace_processor {

base::StatusOr<std::unique_ptr<ChunkedTraceReader>>
TraceReaderRegistry::CreateTraceReader(TraceImporterId id,
                                       TraceProcessorContext* context,
                                       uint32_t file_id) {
  const TraceImporterBase* importer = importers_.FindImporter(id);
  if (!importer) {
    return base::ErrStatus("No reader registered for the detected trace type");
  }
  return importer->CreateReader(context, file_id);
}

}  // namespace perfetto::trace_processor
