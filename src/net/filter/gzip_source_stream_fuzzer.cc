// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/filter/gzip_source_stream.h"

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

  const net::SourceStream::SourceType kGzipTypes[] = {
      net::SourceStream::TYPE_GZIP, net::SourceStream::TYPE_DEFLATE};
  net::SourceStream::SourceType type =
      data_provider.PickValueInArray(kGzipTypes);
  std::unique_ptr<net::GzipSourceStream> gzip_stream =
      net::GzipSourceStream::Create(std::move(fuzzed_source_stream), type);
  while (true) {
    scoped_refptr<net::IOBufferWithSize> io_buffer =
        base::MakeRefCounted<net::IOBufferWithSize>(64);
    int result = gzip_stream->Read(io_buffer.get(), io_buffer->size(),
                                   callback.callback());
    // Releasing the pointer to IOBuffer immediately is more likely to lead to a
    // use-after-free.
    io_buffer = nullptr;
    if (callback.GetResult(result) <= 0)
      break;
  }

  return 0;
}
