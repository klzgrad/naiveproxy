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

#include "src/trace_processor/sqlite/sqlite_export.h"

#include <cstdint>
#include <memory>
#include <string>
#include <utility>

#include "perfetto/base/logging.h"
#include "perfetto/base/status.h"
#include "perfetto/ext/base/status_macros.h"
#include "perfetto/ext/base/string_utils.h"
#include "perfetto/trace_processor/io.h"
#include "perfetto/trace_processor/trace_processor.h"
#include "src/trace_processor/sqlite/file_system_vfs.h"

namespace perfetto::trace_processor {
namespace {

constexpr char kExportDbName[] = "perfetto_export";

std::string MakeFileUri(const std::string& path, const char* vfs_name) {
  std::string uri = "file:";
  for (char c : path) {
    bool unreserved = (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
                      (c >= '0' && c <= '9') || c == '-' || c == '.' ||
                      c == '_' || c == '~' || c == '/' || c == ':';
    if (unreserved) {
      uri.push_back(c);
    } else {
      base::StackString<4> escaped("%%%02X", static_cast<uint8_t>(c));
      uri.append(escaped.c_str());
    }
  }
  uri.append("?vfs=");
  uri.append(vfs_name);
  return uri;
}

base::Status ExecuteToCompletion(TraceProcessor* trace_processor,
                                 const std::string& sql) {
  auto iterator = trace_processor->ExecuteQuery(sql);
  while (iterator.Next()) {
  }
  return iterator.Status();
}

base::Status ExportAttachedDatabase(TraceProcessor* trace_processor) {
  // The export database is disposable and regenerated from the trace on
  // failure, so trade durability for speed and lower I/O.
  RETURN_IF_ERROR(ExecuteToCompletion(
      trace_processor,
      "PRAGMA " + std::string(kExportDbName) + ".journal_mode=OFF"));
  RETURN_IF_ERROR(ExecuteToCompletion(
      trace_processor,
      "PRAGMA " + std::string(kExportDbName) + ".synchronous=OFF"));

  // Preserve the existing SQLite export contract: materialize all tables
  // visible through perfetto_tables, including runtime SQL tables and virtual
  // tables.
  auto tables =
      trace_processor->ExecuteQuery("SELECT name FROM perfetto_tables");
  while (tables.Next()) {
    std::string table_name = tables.Get(0).string_value;
    PERFETTO_CHECK(!base::Contains(table_name, '\''));
    RETURN_IF_ERROR(ExecuteToCompletion(
        trace_processor, "CREATE TABLE " + std::string(kExportDbName) + "." +
                             table_name + " AS SELECT * FROM " + table_name));
  }
  RETURN_IF_ERROR(tables.Status());

  // Preserve SQL views as views over the exported tables.
  auto views = trace_processor->ExecuteQuery(
      "SELECT sql FROM sqlite_master WHERE type='view'");
  while (views.Next()) {
    std::string sql = views.Get(0).string_value;
    const std::string kPrefix = "CREATE VIEW ";
    PERFETTO_CHECK(base::StartsWith(sql, kPrefix));
    sql = sql.substr(0, kPrefix.size()) + std::string(kExportDbName) + "." +
          sql.substr(kPrefix.size());
    RETURN_IF_ERROR(ExecuteToCompletion(trace_processor, sql));
  }
  return views.Status();
}

}  // namespace

base::Status ExportSqliteDatabase(TraceProcessor* trace_processor,
                                  io::FileSystem* file_system,
                                  const std::string& path) {
  if (!trace_processor) {
    return base::ErrStatus("Trace Processor is null");
  }
  if (!file_system) {
    return base::ErrStatus("SQLite export filesystem is null");
  }
  if (path.empty()) {
    return base::ErrStatus("SQLite export path is empty");
  }

  io::FileOpenOptions options;
  options.access = io::FileAccess::kReadWrite;
  options.create = true;
  options.truncate = true;
  std::unique_ptr<io::File> output;
  RETURN_IF_ERROR(file_system->OpenFile(path, options, &output));
  output.reset();

  ASSIGN_OR_RETURN(auto vfs, SqliteFileSystemVfs::Create(file_system));
  std::string uri = MakeFileUri(path, vfs->name());
  RETURN_IF_ERROR(ExecuteToCompletion(
      trace_processor, "ATTACH DATABASE '" + uri + "' AS " + kExportDbName));

  base::Status export_status = ExportAttachedDatabase(trace_processor);
  base::Status detach_status = ExecuteToCompletion(
      trace_processor, "DETACH DATABASE " + std::string(kExportDbName));
  if (!detach_status.ok()) {
    // The connection can still hold sqlite3_file objects which reference this
    // VFS. Keep the VFS registered rather than leaving dangling callbacks.
    base::ignore_result(vfs.release());
  }
  if (!export_status.ok()) {
    return export_status;
  }
  return detach_status;
}

}  // namespace perfetto::trace_processor
