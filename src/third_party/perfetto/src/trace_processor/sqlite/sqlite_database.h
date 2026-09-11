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

#ifndef SRC_TRACE_PROCESSOR_SQLITE_SQLITE_DATABASE_H_
#define SRC_TRACE_PROCESSOR_SQLITE_SQLITE_DATABASE_H_

#include <atomic>
#include <cstdint>
#include <string>

#include "perfetto/ext/base/string_utils.h"

namespace perfetto::trace_processor {

// Logical handle to a shared in-memory SQLite database. Hands out a URI that
// |SqliteConnection|s open to attach to the same backing store; carries no
// state of its own.
//
// The backing store is owned by the SQLite memdb VFS for as long as at least
// one connection has it open, and is freed when the last connection closes.
// Callers are responsible for keeping the |SqliteDatabase| alive for at least
// as long as any connection that holds onto its URI.
class SqliteDatabase {
 public:
  SqliteDatabase()
      : shared_filename_("file:/perfetto-" +
                         base::Uint64ToHexStringNoPrefix(NextDatabaseId()) +
                         "?vfs=memdb") {}

  SqliteDatabase(const SqliteDatabase&) = delete;
  SqliteDatabase& operator=(const SqliteDatabase&) = delete;

  // URI identifying this database's shared memdb backing store. Connections
  // that wish to attach to this database open this URI.
  const std::string& shared_filename() const { return shared_filename_; }

 private:
  static uint64_t NextDatabaseId() {
    static std::atomic<uint64_t> next{0};
    return next.fetch_add(1, std::memory_order_relaxed);
  }

  std::string shared_filename_;
};

}  // namespace perfetto::trace_processor

#endif  // SRC_TRACE_PROCESSOR_SQLITE_SQLITE_DATABASE_H_
