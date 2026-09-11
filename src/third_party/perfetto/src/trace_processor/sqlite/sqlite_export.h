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

#ifndef SRC_TRACE_PROCESSOR_SQLITE_SQLITE_EXPORT_H_
#define SRC_TRACE_PROCESSOR_SQLITE_SQLITE_EXPORT_H_

#include <memory>
#include <string>

#include "perfetto/base/status.h"

namespace perfetto::trace_processor {

namespace io {
class FileSystem;
}  // namespace io

class TraceProcessor;

// Exports the current SQL-visible tables and views into a SQLite database on
// |file_system|. |file_system| is borrowed and must outlive the call. The
// database at |path| is replaced.
base::Status ExportSqliteDatabase(TraceProcessor*,
                                  io::FileSystem* file_system,
                                  const std::string& path);

}  // namespace perfetto::trace_processor

#endif  // SRC_TRACE_PROCESSOR_SQLITE_SQLITE_EXPORT_H_
