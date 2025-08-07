// Copyright 2016 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef NET_FILTER_FUZZED_SOURCE_STREAM_H_
#define NET_FILTER_FUZZED_SOURCE_STREAM_H_

#include <string>

#include "base/memory/raw_ptr.h"
#include "base/memory/scoped_refptr.h"
#include "net/base/completion_once_callback.h"
#include "net/filter/source_stream.h"

class FuzzedDataProvider;

namespace net {

class IOBuffer;

// A SourceStream implementation used in tests. This allows tests to specify
// what data to return for each Read() call.
class FuzzedSourceStream : public SourceStream {
 public:
  // |data_provider| is used to determine behavior of the FuzzedSourceStream.
  // It must remain valid until after the FuzzedSocket is destroyed.
  explicit FuzzedSourceStream(FuzzedDataProvider* data_provider);

  FuzzedSourceStream(const FuzzedSourceStream&) = delete;
  FuzzedSourceStream& operator=(const FuzzedSourceStream&) = delete;

  ~FuzzedSourceStream() override;

  // SourceStream implementation
  int Read(IOBuffer* dest_buffer,
           int buffer_size,
           CompletionOnceCallback callback) override;
  std::string Description() const override;
  bool MayHaveMoreBytes() const override;

 private:
  void OnReadComplete(CompletionOnceCallback callback,
                      const std::string& fuzzed_data,
                      scoped_refptr<IOBuffer> read_buf,
                      int result);

  raw_ptr<FuzzedDataProvider> data_provider_;

  // Whether there is a pending Read().
  bool read_pending_ = false;

  // Last result returned by Read() is an error or 0.
  bool end_returned_ = false;
};

}  // namespace net

#endif  // NET_FILTER_FUZZED_SOURCE_STREAM_H_
