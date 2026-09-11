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

#include "src/trace_processor/importers/archive/archive_entry.h"

#include <cstddef>
#include <string>
#include <tuple>

namespace perfetto::trace_processor {

bool IsHiddenArchivePath(const std::string& path) {
  size_t start = 0;
  while (start <= path.size()) {
    size_t slash = path.find('/', start);
    size_t end = slash == std::string::npos ? path.size() : slash;
    // Inspect the component [start, end).
    if (end > start && path[start] == '.') {
      // "." and ".." are current/parent directory segments, not hidden files.
      bool is_dot = (end - start) == 1;
      bool is_dot_dot = (end - start) == 2 && path[start + 1] == '.';
      if (!is_dot && !is_dot_dot) {
        return true;
      }
    }
    if (slash == std::string::npos) {
      break;
    }
    start = slash + 1;
  }
  return false;
}

int ArchiveEntry::ComputePriority(TraceImporterId type,
                                  const TraceImporterRegistry& registry) {
  return registry.Find(type)->archive_priority;
}

bool ArchiveEntry::operator<(const ArchiveEntry& rhs) const {
  // Compare first by archive priority, then by name, and finally by index to
  // ensure strict ordering.
  return std::tie(priority, name, index) <
         std::tie(rhs.priority, rhs.name, rhs.index);
}

}  // namespace perfetto::trace_processor
