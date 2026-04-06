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

#include "src/trace_processor/util/tar_writer.h"

#include <fcntl.h>
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <string>
#include <utility>

#include "perfetto/base/logging.h"
#include "perfetto/base/status.h"
#include "perfetto/ext/base/file_utils.h"
#include "perfetto/ext/base/scoped_file.h"
#include "perfetto/ext/base/status_macros.h"
#include "perfetto/ext/base/string_utils.h"

namespace perfetto::trace_processor::util {

namespace {
// Helper function to safely copy from constant string arrays into fixed-size
// char arrays
template <size_t DestN, size_t SrcN>
void SafeCopyToCharArray(char (&dest)[DestN], const char (&src)[SrcN]) {
  static_assert(SrcN - 1 <= DestN,
                "Source string too long for destination array");
  constexpr size_t copy_len =
      SrcN - 1;  // -1 to exclude null terminator from src
  memcpy(dest, src, copy_len);
  // Zero-fill the rest
  if constexpr (copy_len < DestN) {
    memset(dest + copy_len, 0, DestN - copy_len);
  }
}
}  // namespace

TarWriter::TarWriter(const std::string& output_path)
    : TarWriter(
          base::OpenFile(output_path, O_CREAT | O_WRONLY | O_TRUNC, 0644)) {}

TarWriter::TarWriter(base::ScopedFile output_file)
    : output_file_(std::move(output_file)) {
  PERFETTO_CHECK(output_file_);
}

TarWriter::~TarWriter() {
  // Write two 512-byte blocks of zeros to mark end of archive
  char zero_block[512] = {0};
  ssize_t written1 = base::WriteAll(output_file_.get(), zero_block, 512);
  PERFETTO_CHECK(written1 == 512);

  ssize_t written2 = base::WriteAll(output_file_.get(), zero_block, 512);
  PERFETTO_CHECK(written2 == 512);
}

base::Status TarWriter::AddFile(const std::string& filename,
                                const std::string& content) {
  RETURN_IF_ERROR(ValidateFilename(filename));
  RETURN_IF_ERROR(CreateAndWriteHeader(filename, content.size()));

  // Write file content
  ssize_t bytes_written =
      base::WriteAll(output_file_.get(), content.data(), content.size());
  if (bytes_written != static_cast<ssize_t>(content.size())) {
    return base::Status("Failed to write file content");
  }

  // Write padding to align to 512-byte boundary
  RETURN_IF_ERROR(WritePadding(content.size()));

  return base::OkStatus();
}

base::Status TarWriter::AddFileFromPath(const std::string& filename,
                                        const std::string& file_path) {
  RETURN_IF_ERROR(ValidateFilename(filename));

  // Get file size
  auto file_size_opt = base::GetFileSize(file_path);
  if (!file_size_opt) {
    return base::Status("Failed to get file size: " + file_path);
  }
  size_t file_size = static_cast<size_t>(*file_size_opt);

  base::ScopedFile file = base::OpenFile(file_path, O_RDONLY);
  if (!file) {
    return base::Status("Failed to open file: " + file_path);
  }

  RETURN_IF_ERROR(CreateAndWriteHeader(filename, file_size));

  RETURN_IF_ERROR(base::CopyFileContents(*file, *output_file_));

  // Write padding to align to 512-byte boundary
  RETURN_IF_ERROR(WritePadding(file_size));

  return base::OkStatus();
}

base::Status TarWriter::CreateAndWriteHeader(const std::string& filename,
                                             size_t file_size) {
  TarHeader header;

  // Initialize header
  memset(&header, 0, sizeof(TarHeader));
  SafeCopyToCharArray(header.mode, "0644   ");   // Regular file, rw-r--r--
  SafeCopyToCharArray(header.uid, "0000000");    // Root user
  SafeCopyToCharArray(header.gid, "0000000");    // Root group
  header.typeflag = '0';                         // Regular file
  SafeCopyToCharArray(header.magic, "ustar\0");  // POSIX ustar format
  SafeCopyToCharArray(header.version, "00");     // Version
  SafeCopyToCharArray(header.uname, "root");     // User name
  SafeCopyToCharArray(header.gname, "root");     // Group name
  SafeCopyToCharArray(header.devmajor, "0000000");
  SafeCopyToCharArray(header.devminor, "0000000");
  memset(header.checksum, ' ', sizeof(header.checksum));

  // Set filename
  base::StringCopy(header.name, filename.c_str(), sizeof(header.name));

  // Set file size (in octal)
  snprintf(header.size, sizeof(header.size), "%011lo",
           static_cast<unsigned long>(file_size));

  // Set modification time to current time (in octal)
  snprintf(header.mtime, sizeof(header.mtime), "%011lo",
           static_cast<unsigned long>(time(nullptr)));

  // Compute checksum
  unsigned int sum = 0;
  const unsigned char* bytes = reinterpret_cast<const unsigned char*>(&header);
  for (size_t i = 0; i < sizeof(TarHeader); i++) {
    sum += bytes[i];
  }
  snprintf(header.checksum, sizeof(header.checksum), "%06o", sum);
  header.checksum[6] = '\0';
  header.checksum[7] = ' ';

  // Write header
  ssize_t written =
      base::WriteAll(output_file_.get(), reinterpret_cast<const char*>(&header),
                     sizeof(header));
  if (written != static_cast<ssize_t>(sizeof(header))) {
    return base::Status("Failed to write TAR header");
  }
  return base::OkStatus();
}

base::Status TarWriter::WritePadding(size_t size) {
  // TAR files must be padded to 512-byte boundaries
  size_t padding_needed = (512 - (size % 512)) % 512;
  if (padding_needed > 0) {
    char zeros[512] = {0};
    ssize_t written = base::WriteAll(output_file_.get(), zeros, padding_needed);
    if (written != static_cast<ssize_t>(padding_needed)) {
      return base::Status("Failed to write TAR padding");
    }
  }
  return base::OkStatus();
}

base::Status TarWriter::ValidateFilename(const std::string& filename) {
  // TAR header name field is 100 bytes, but we need null termination
  if (filename.empty()) {
    return base::Status("Filename cannot be empty");
  }
  if (filename.length() > 99) {
    return base::Status("Filename too long for TAR format (max 99 chars): " +
                        filename);
  }
  // Check for invalid characters that might cause issues
  if (filename.find('\0') != std::string::npos) {
    return base::Status("Filename contains null character: " + filename);
  }
  return base::OkStatus();
}

}  // namespace perfetto::trace_processor::util
