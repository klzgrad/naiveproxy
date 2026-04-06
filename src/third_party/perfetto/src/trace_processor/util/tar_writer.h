/*
 * Copyright (C) 2025 The Android Open Source Project
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

#ifndef SRC_TRACE_PROCESSOR_UTIL_TAR_WRITER_H_
#define SRC_TRACE_PROCESSOR_UTIL_TAR_WRITER_H_

#include <cstddef>
#include <string>

#include "perfetto/base/status.h"
#include "perfetto/ext/base/scoped_file.h"

namespace perfetto::trace_processor::util {

// Simple TAR writer that creates uncompressed TAR archives.
//
// Implements the POSIX ustar format for maximum compatibility:
// - Supported by all modern TAR implementations
// - Simple structure with fixed 512-byte blocks
// - No compression (keeps implementation simple and fast)
// - Supports files up to ~8GB with standard ustar format
//
// The ustar format was chosen over other TAR variants because:
// - GNU TAR extensions would limit compatibility
// - pax format adds complexity for minimal benefit in our use case
// - Original TAR format has more limitations (no long filenames)
class TarWriter {
 public:
  explicit TarWriter(const std::string& output_path);
  explicit TarWriter(base::ScopedFile output_file);
  ~TarWriter();

  // Adds a file to the TAR archive.
  // filename: The name of the file in the archive (max 100 chars)
  // content: The file content
  // Returns OkStatus() on success, error Status on failure.
  base::Status AddFile(const std::string& filename, const std::string& content);

  // Adds a file to the TAR archive from a file path.
  // filename: The name of the file in the archive (max 100 chars)
  // file_path: Path to the file to add
  // Returns OkStatus() on success, error Status on failure.
  base::Status AddFileFromPath(const std::string& filename,
                               const std::string& file_path);

 private:
  // TAR header structure (512 bytes)
  struct TarHeader {
    char name[100];      // File name
    char mode[8];        // File mode (octal)
    char uid[8];         // User ID (octal)
    char gid[8];         // Group ID (octal)
    char size[12];       // File size in bytes (octal)
    char mtime[12];      // Modification time (octal)
    char checksum[8];    // Header checksum
    char typeflag;       // File type
    char linkname[100];  // Name of linked file
    char magic[6];       // USTAR indicator
    char version[2];     // USTAR version
    char uname[32];      // User name
    char gname[32];      // Group name
    char devmajor[8];    // Device major number
    char devminor[8];    // Device minor number
    char prefix[155];    // Filename prefix
    char padding[12];    // Padding to 512 bytes
  };
  static_assert(sizeof(TarHeader) == 512, "TarHeader must be 512 bytes");

  base::Status ValidateFilename(const std::string& filename);
  base::Status CreateAndWriteHeader(const std::string& filename,
                                    size_t file_size);
  base::Status WritePadding(size_t size);

  base::ScopedFile output_file_;
};

}  // namespace perfetto::trace_processor::util

#endif  // SRC_TRACE_PROCESSOR_UTIL_TAR_WRITER_H_
