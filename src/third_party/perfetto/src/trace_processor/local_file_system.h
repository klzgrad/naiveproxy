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

#ifndef SRC_TRACE_PROCESSOR_LOCAL_FILE_SYSTEM_H_
#define SRC_TRACE_PROCESSOR_LOCAL_FILE_SYSTEM_H_

#include <memory>

#include "perfetto/trace_processor/io.h"

namespace perfetto::trace_processor::io {

// Returns a process-wide filesystem backed by the native filesystem.
FileSystem* CreateLocalFileSystem();

// Returns a process-wide filesystem which rejects every operation. Embedders
// without file I/O use this explicitly rather than accidentally falling
// through to process filesystem calls.
FileSystem* CreateNoopFileSystem();

}  // namespace perfetto::trace_processor::io

#endif  // SRC_TRACE_PROCESSOR_LOCAL_FILE_SYSTEM_H_
