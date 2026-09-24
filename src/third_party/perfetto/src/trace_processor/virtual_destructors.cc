/*
 * Copyright (C) 2018 The Android Open Source Project
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

#include "perfetto/trace_processor/trace_processor.h"
#include "src/trace_processor/importers/common/chunked_trace_reader.h"

namespace perfetto {
namespace trace_processor {

ChunkedTraceReader::~ChunkedTraceReader() {}

// Anchored here: embedders linking only "storage_minimal" call
// GetFileSystem() and still need the vtable.
TraceProcessor_PlatformInterface::~TraceProcessor_PlatformInterface() = default;

}  // namespace trace_processor
}  // namespace perfetto
