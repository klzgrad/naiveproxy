// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/filter/gzip_source_stream.h"

#include <algorithm>

#include "base/logging.h"
#include "base/memory/ref_counted.h"
#include "base/test/fuzzed_data_provider.h"
#include "net/base/io_buffer.h"
#include "net/base/test_completion_callback.h"
#include "net/filter/fuzzed_source_stream.h"

// Fuzzer for GzipSourceStream.
//
// |data| is used to create a FuzzedSourceStream.
extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
  net::TestCompletionCallback callback;
  base::FuzzedDataProvider data_provider(data, size);
  std::unique_ptr<net::FuzzedSourceStream> fuzzed_source_stream(
      new net::FuzzedSourceStream(&data_provider));

  // Gzip has a maximum compression ratio of 1032x. While, strictly speaking,
  // linear, this means the fuzzer will often get stuck. Stop reading at a more
  // modest compression ratio of 2x, or 512 KiB, whichever is larger. See
  // https://crbug.com/921075.
  size_t max_output = std::max(2u * size, static_cast<size_t>(512 * 1024));

  const net::SourceStream::SourceType kGzipTypes[] = {
      net::SourceStream::TYPE_GZIP, net::SourceStream::TYPE_DEFLATE};
  net::SourceStream::SourceType type =
      data_provider.PickValueInArray(kGzipTypes);
  std::unique_ptr<net::GzipSourceStream> gzip_stream =
      net::GzipSourceStream::Create(std::move(fuzzed_source_stream), type);
  size_t bytes_read = 0;
  while (true) {
    scoped_refptr<net::IOBufferWithSize> io_buffer =
        base::MakeRefCounted<net::IOBufferWithSize>(64);
    int result = gzip_stream->Read(io_buffer.get(), io_buffer->size(),
                                   callback.callback());
    // Releasing the pointer to IOBuffer immediately is more likely to lead to a
    // use-after-free.
    io_buffer = nullptr;
    result = callback.GetResult(result);
    if (result <= 0)
      break;
    bytes_read += static_cast<size_t>(result);
    if (bytes_read >= max_output)
      break;
  }

  return 0;
}
