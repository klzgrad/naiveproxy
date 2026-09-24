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
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "perfetto/base/status.h"
#include "perfetto/ext/base/scoped_file.h"
#include "perfetto/ext/base/status_or.h"

namespace perfetto::trace_processor::util {

// Destination for the bytes produced by a TarWriter.
class TarWriterSink {
 public:
  virtual ~TarWriterSink();
  virtual base::Status Write(const void* data, size_t len) = 0;
  // Copies len bytes from fd to the sink. Default implementation may read
  // into a buffer and call Write; fd-backed sinks can override with an
  // efficient fd-to-fd copy.
  virtual base::Status WriteFromFd(int fd, size_t len) = 0;
};

// Appends written bytes to an in-memory buffer.
class BufferTarWriterSink : public TarWriterSink {
 public:
  explicit BufferTarWriterSink(std::vector<uint8_t>* buffer);
  base::Status Write(const void* data, size_t len) override;
  base::Status WriteFromFd(int fd, size_t len) override;

 private:
  std::vector<uint8_t>* buffer_;
};

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
  explicit TarWriter(std::unique_ptr<TarWriterSink> sink);
  // Finalizes the archive if Finalize() was not already called and crashes
  // if that write fails. Call Finalize() explicitly to handle failures.
  ~TarWriter();
  TarWriter(const TarWriter&) = delete;
  TarWriter& operator=(const TarWriter&) = delete;

  // Adds a file to the TAR archive.
  // filename: The name of the file in the archive (max 100 chars)
  // content: The file content
  // Returns OkStatus() on success, error Status on failure.
  base::Status AddFile(const std::string& filename, const std::string& content);

  // Adds a file to the TAR archive from raw bytes.
  base::Status AddFile(const std::string& filename,
                       const uint8_t* data,
                       size_t size);

  // Adds a file to the TAR archive from a file path.
  // filename: The name of the file in the archive (max 100 chars)
  // file_path: Path to the file to add
  // Returns OkStatus() on success, error Status on failure.
  base::Status AddFileFromPath(const std::string& filename,
                               const std::string& file_path);

  // Handle for streaming a single file's content into the archive without
  // buffering it all in memory first. Returned by BeginFile(). Move-only.
  class ScopedFileWriter {
   public:
    ScopedFileWriter(ScopedFileWriter&&) noexcept;
    ScopedFileWriter& operator=(ScopedFileWriter&&) noexcept;
    // Finalizes the entry if Finalize() was not already called and crashes
    // if that write fails. Call Finalize() explicitly to handle failures.
    ~ScopedFileWriter();
    ScopedFileWriter(const ScopedFileWriter&) = delete;
    ScopedFileWriter& operator=(const ScopedFileWriter&) = delete;

    // A failed write poisons both the entry and the TarWriter: the archive
    // is corrupt at that point, so all further finalization becomes a no-op.
    // Must not be called after Finalize(); doing so crashes.
    base::Status Write(const void* data, size_t len);

    // Copies `len` bytes from `fd` into the archive.
    base::Status WriteFromFd(int fd, size_t len);

    // Validates that exactly the `size` bytes passed to StreamFile() were
    // written, then writes the 512-byte-boundary padding.
    base::Status Finalize();

   private:
    friend class TarWriter;
    ScopedFileWriter(TarWriter* writer, size_t size);

    base::Status CheckCanWrite(size_t len);

    // Null once the entry is finalized or moved-from.
    TarWriter* writer_;
    size_t size_;
    size_t bytes_written_ = 0;
  };

  // Writes the TAR header for `filename` immediately and returns a writer
  // for streaming exactly `size` bytes of content.
  base::StatusOr<ScopedFileWriter> StreamFile(const std::string& filename,
                                              size_t size);

  // Writes the two zero end-of-archive blocks. Idempotent: calls after the
  // first always return OkStatus() without writing anything further. Also a
  // no-op once any write to the sink has failed: the archive is already
  // corrupt, so nothing further is written on teardown.
  base::Status Finalize();

 private:
  // All sink writes go through these: a failure poisons the writer via
  // PoisonIfError() so no further writes are attempted on teardown.
  base::Status WriteToSink(const void* data, size_t len);
  base::Status WriteFromFdToSink(int fd, size_t len);
  base::Status PoisonIfError(base::Status status);

  std::unique_ptr<TarWriterSink> sink_;
  bool finalized_ = false;
};

}  // namespace perfetto::trace_processor::util

#endif  // SRC_TRACE_PROCESSOR_UTIL_TAR_WRITER_H_
