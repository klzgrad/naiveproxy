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

#ifndef SRC_TRACE_PROCESSOR_IMPORTERS_ARCHIVE_ARCHIVE_ENTRY_H_
#define SRC_TRACE_PROCESSOR_IMPORTERS_ARCHIVE_ARCHIVE_ENTRY_H_

#include <cstddef>
#include <string>

#include "src/trace_processor/util/trace_type.h"

namespace perfetto::trace_processor {

// Returns true if `path` should be ignored when reading an archive (tar/zip),
// i.e. if any of its path components starts with a '.' (other than the "." and
// ".." path segments). Such entries are hidden files and directories which are
// never traces: the most common cases are the AppleDouble resource-fork files
// ("._foo") and ".DS_Store" entries that macOS/BSD tar and Finder-created zips
// sprinkle alongside the real files. Ignoring them lets archives created on
// macOS load without spurious "unknown trace type" failures.
bool IsHiddenArchivePath(const std::string& path);

// Helper class to determine a proper tokenization. This class can be used as
// a key of a std::map to automatically sort files before sending them in proper
// order for tokenization.
struct ArchiveEntry {
  // Returns the archive ordering priority for `type` (lower is read first):
  // manifest < proto < containers < others < symbols.
  static int ComputePriority(TraceImporterId type,
                             const TraceImporterRegistry& registry);

  // File name. Used to break ties.
  std::string name;
  // Position. Used to break ties.
  size_t index;
  // Trace type. Kept for the reader to cross-check the detected type.
  TraceImporterId trace_type;
  // Archive ordering priority derived from trace_type (see ComputePriority).
  // This is the main attribute traces are ordered by; proto is read first as it
  // may contain clock sync data needed to correctly parse other traces.
  int priority;
  // Comparator used to determine the order in which files in the ZIP will be
  // read.
  bool operator<(const ArchiveEntry& rhs) const;
};
}  // namespace perfetto::trace_processor

#endif  // SRC_TRACE_PROCESSOR_IMPORTERS_ARCHIVE_ARCHIVE_ENTRY_H_
