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
#include <cerrno>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <memory>
#include <new>
#include <string>
#include <utility>
#include <vector>

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

base::Status ValidateFilename(const std::string& filename) {
  // TAR header name field is 100 bytes, but we need null termination
  if (filename.empty()) {
    return base::ErrStatus("Filename cannot be empty");
  }
  if (filename.length() > 99) {
    return base::ErrStatus(
        "Filename too long for TAR format (max 99 chars): %s",
        filename.c_str());
  }
  // Check for invalid characters that might cause issues
  if (filename.find('\0') != std::string::npos) {
    return base::ErrStatus("Filename contains null character: %s",
                           filename.c_str());
  }
  return base::OkStatus();
}

TarHeader MakeTarHeader(const std::string& filename, size_t file_size) {
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

  return header;
}

}  // namespace

// --- TarWriterSink ---

TarWriterSink::~TarWriterSink() = default;

namespace {

// Writes to a file descriptor. Backs the ScopedFile constructor.
class FdTarWriterSink : public TarWriterSink {
 public:
  explicit FdTarWriterSink(base::ScopedFile fd) : fd_(std::move(fd)) {
    PERFETTO_CHECK(fd_);
  }

  base::Status Write(const void* data, size_t len) override {
    ssize_t written = base::WriteAll(fd_.get(), data, len);
    if (written != static_cast<ssize_t>(len)) {
      return base::ErrStatus("Failed to write to TAR output");
    }
    return base::OkStatus();
  }

  base::Status WriteFromFd(int fd, size_t) override {
    return base::CopyFileContents(fd, *fd_);
  }

 private:
  base::ScopedFile fd_;
};

// Returns the same error for every operation. Used when the output path
// could not be opened, so that callers get a useful error instead of a
// PERFETTO_CHECK crash when they attempt the first write.
class FailedTarWriterSink : public TarWriterSink {
 public:
  explicit FailedTarWriterSink(base::Status error) : error_(std::move(error)) {}

  base::Status Write(const void*, size_t) override { return error_; }
  base::Status WriteFromFd(int, size_t) override { return error_; }

 private:
  base::Status error_;
};

}  // namespace

// --- BufferTarWriterSink ---

BufferTarWriterSink::BufferTarWriterSink(std::vector<uint8_t>* buffer)
    : buffer_(buffer) {
  PERFETTO_CHECK(buffer_);
}

base::Status BufferTarWriterSink::Write(const void* data, size_t len) {
  const auto* bytes = static_cast<const uint8_t*>(data);
  buffer_->insert(buffer_->end(), bytes, bytes + len);
  return base::OkStatus();
}

base::Status BufferTarWriterSink::WriteFromFd(int fd, size_t len) {
  size_t old_size = buffer_->size();
  buffer_->resize(old_size + len);
  ssize_t rd = base::Read(fd, buffer_->data() + old_size, len);
  if (rd != static_cast<ssize_t>(len)) {
    buffer_->resize(old_size);
    return base::ErrStatus("Failed to read from fd");
  }
  return base::OkStatus();
}

// --- TarWriter ---

namespace {

// Opens the output path for writing. On failure returns a sink that makes
// every subsequent TarWriter operation fail with a descriptive error
// (instead of crashing on a PERFETTO_CHECK as the old code did).
std::unique_ptr<TarWriterSink> OpenOutputSink(const std::string& output_path) {
  base::ScopedFile fd =
      base::OpenFile(output_path, O_CREAT | O_WRONLY | O_TRUNC, 0644);
  if (fd) {
    return std::unique_ptr<TarWriterSink>(new FdTarWriterSink(std::move(fd)));
  }
  return std::unique_ptr<TarWriterSink>(new FailedTarWriterSink(
      base::ErrStatus("Failed to open output file '%s' for writing (errno: "
                      "%d, %s)",
                      output_path.c_str(), errno, strerror(errno))));
}

}  // namespace

TarWriter::TarWriter(const std::string& output_path)
    : TarWriter(OpenOutputSink(output_path)) {}

TarWriter::TarWriter(base::ScopedFile output_file)
    : TarWriter(std::unique_ptr<TarWriterSink>(
          new FdTarWriterSink(std::move(output_file)))) {}

TarWriter::TarWriter(std::unique_ptr<TarWriterSink> sink)
    : sink_(std::move(sink)) {
  PERFETTO_CHECK(sink_);
}

TarWriter::~TarWriter() {
  base::Status status = Finalize();
  PERFETTO_CHECK(status.ok());
}

base::Status TarWriter::Finalize() {
  if (finalized_) {
    return base::OkStatus();
  }
  finalized_ = true;
  // Write two 512-byte blocks of zeros to mark end of archive.
  char zero_block[512] = {0};
  RETURN_IF_ERROR(WriteToSink(zero_block, sizeof(zero_block)));
  RETURN_IF_ERROR(WriteToSink(zero_block, sizeof(zero_block)));
  return base::OkStatus();
}

base::Status TarWriter::WriteToSink(const void* data, size_t len) {
  return PoisonIfError(sink_->Write(data, len));
}

base::Status TarWriter::WriteFromFdToSink(int fd, size_t len) {
  return PoisonIfError(sink_->WriteFromFd(fd, len));
}

base::Status TarWriter::PoisonIfError(base::Status status) {
  if (!status.ok()) {
    finalized_ = true;
  }
  return status;
}

base::Status TarWriter::AddFile(const std::string& filename,
                                const std::string& content) {
  return AddFile(filename, reinterpret_cast<const uint8_t*>(content.data()),
                 content.size());
}

base::Status TarWriter::AddFile(const std::string& filename,
                                const uint8_t* data,
                                size_t size) {
  ASSIGN_OR_RETURN(ScopedFileWriter file, StreamFile(filename, size));
  RETURN_IF_ERROR(file.Write(data, size));
  return file.Finalize();
}

base::Status TarWriter::AddFileFromPath(const std::string& filename,
                                        const std::string& file_path) {
  auto file_size_opt = base::GetFileSize(file_path);
  if (!file_size_opt) {
    return base::ErrStatus("Failed to get file size: %s", file_path.c_str());
  }
  size_t file_size = static_cast<size_t>(*file_size_opt);

  base::ScopedFile fd = base::OpenFile(file_path, O_RDONLY);
  if (!fd) {
    return base::ErrStatus("Failed to open file: %s", file_path.c_str());
  }

  ASSIGN_OR_RETURN(ScopedFileWriter file, StreamFile(filename, file_size));
  RETURN_IF_ERROR(file.WriteFromFd(*fd, file_size));
  return file.Finalize();
}

base::StatusOr<TarWriter::ScopedFileWriter> TarWriter::StreamFile(
    const std::string& filename,
    size_t size) {
  RETURN_IF_ERROR(ValidateFilename(filename));
  TarHeader header = MakeTarHeader(filename, size);
  RETURN_IF_ERROR(WriteToSink(&header, sizeof(header)));
  return ScopedFileWriter(this, size);
}

// --- TarWriter::ScopedFileWriter ---

TarWriter::ScopedFileWriter::ScopedFileWriter(TarWriter* writer, size_t size)
    : writer_(writer), size_(size) {}

TarWriter::ScopedFileWriter::ScopedFileWriter(ScopedFileWriter&& other) noexcept
    : writer_(other.writer_),
      size_(other.size_),
      bytes_written_(other.bytes_written_) {
  other.writer_ = nullptr;
}

TarWriter::ScopedFileWriter& TarWriter::ScopedFileWriter::operator=(
    ScopedFileWriter&& other) noexcept {
  if (this != &other) {
    this->~ScopedFileWriter();
    new (this) ScopedFileWriter(std::move(other));
  }
  return *this;
}

TarWriter::ScopedFileWriter::~ScopedFileWriter() {
  if (!writer_) {
    return;
  }
  base::Status status = Finalize();
  PERFETTO_CHECK(status.ok());
}

base::Status TarWriter::ScopedFileWriter::CheckCanWrite(size_t len) {
  PERFETTO_CHECK(writer_);
  PERFETTO_DCHECK(bytes_written_ <= size_);
  if (len > size_ - bytes_written_) {
    return writer_->PoisonIfError(base::ErrStatus(
        "Cannot write %zu bytes to TAR entry: only %zu bytes remain", len,
        size_ - bytes_written_));
  }
  return base::OkStatus();
}

base::Status TarWriter::ScopedFileWriter::Write(const void* data, size_t len) {
  RETURN_IF_ERROR(CheckCanWrite(len));
  RETURN_IF_ERROR(writer_->WriteToSink(data, len));
  bytes_written_ += len;
  return base::OkStatus();
}

base::Status TarWriter::ScopedFileWriter::WriteFromFd(int fd, size_t len) {
  RETURN_IF_ERROR(CheckCanWrite(len));
  RETURN_IF_ERROR(writer_->WriteFromFdToSink(fd, len));
  bytes_written_ += len;
  return base::OkStatus();
}

base::Status TarWriter::ScopedFileWriter::Finalize() {
  // A poisoned writer means the archive is already corrupt: padding it is
  // pointless, so this becomes a no-op.
  if (!writer_ || writer_->finalized_) {
    writer_ = nullptr;
    return base::OkStatus();
  }
  TarWriter* writer = writer_;
  writer_ = nullptr;
  if (bytes_written_ != size_) {
    return writer->PoisonIfError(base::ErrStatus(
        "TAR entry expected %zu bytes, but only %zu were written", size_,
        bytes_written_));
  }
  size_t padding_needed = (512 - (size_ % 512)) % 512;
  if (padding_needed == 0) {
    return base::OkStatus();
  }
  char zeros[512] = {0};
  return writer->WriteToSink(zeros, padding_needed);
}

}  // namespace perfetto::trace_processor::util
