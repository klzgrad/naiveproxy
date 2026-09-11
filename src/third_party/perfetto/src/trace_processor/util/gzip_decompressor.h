/*
 * Copyright (C) 2020 The Android Open Source Project
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

#ifndef SRC_TRACE_PROCESSOR_UTIL_GZIP_DECOMPRESSOR_H_
#define SRC_TRACE_PROCESSOR_UTIL_GZIP_DECOMPRESSOR_H_

#include <cstddef>
#include <cstdint>
#include <memory>

#include "src/trace_processor/util/decompressor.h"

struct z_stream_s;

namespace perfetto::trace_processor::util {

// gzip/deflate Decompressor. Obtain one via CreateDecompressor(); construct
// directly only to pin the InputMode (e.g. raw deflate for zip entries).
class GzipDecompressor : public Decompressor {
 public:
  enum class InputMode {
    // The input stream contains a gzip header. This is for the common case of
    // decompressing .gz files.
    kGzip = 0,

    // A raw deflate stream. This is for the case of uncompressing files from
    // a .zip archive, where the compression type is specified in the zip file
    // entry, rather than in the stream header.
    kRawDeflate = 1,
  };

  explicit GzipDecompressor(InputMode = InputMode::kGzip);

  void Feed(const uint8_t* data, size_t size) final;
  Result ExtractOutput(uint8_t* out, size_t out_capacity) final;
  void Reset() final;
  size_t AvailIn() const final;

 private:
  struct Deleter {
    void operator()(z_stream_s*) const;
  };
  std::unique_ptr<z_stream_s, Deleter> z_stream_;
};

}  // namespace perfetto::trace_processor::util

#endif  // SRC_TRACE_PROCESSOR_UTIL_GZIP_DECOMPRESSOR_H_
