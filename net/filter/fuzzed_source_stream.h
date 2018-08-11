// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef NET_FILTER_FUZZED_SOURCE_STREAM_H_
#define NET_FILTER_FUZZED_SOURCE_STREAM_H_

#include <string>

#include "base/macros.h"
#include "base/memory/ref_counted.h"
#include "net/base/completion_callback.h"
#include "net/filter/source_stream.h"

namespace base {
class FuzzedDataProvider;
}  // namespace base

namespace net {

class IOBuffer;

// A SourceStream implementation used in tests. This allows tests to specify
// what data to return for each Read() call.
class FuzzedSourceStream : public SourceStream {
 public:
  // |data_provider| is used to determine behavior of the FuzzedSourceStream.
  // It must remain valid until after the FuzzedSocket is destroyed.
  explicit FuzzedSourceStream(base::FuzzedDataProvider* data_provider);
  ~FuzzedSourceStream() override;

  // SourceStream implementation
  int Read(IOBuffer* dest_buffer,
           int buffer_size,
           const CompletionCallback& callback) override;
  std::string Description() const override;

 private:
  void OnReadComplete(const CompletionCallback& callback,
                      const std::string& fuzzed_data,
                      scoped_refptr<IOBuffer> read_buf,
                      int result);

  base::FuzzedDataProvider* data_provider_;

  // Whether there is a pending Read().
  bool read_pending_;

  // Last result returned by Read() is an error or 0.
  bool end_returned_;

  DISALLOW_COPY_AND_ASSIGN(FuzzedSourceStream);
};

}  // namespace net

#endif  // NET_FILTER_FUZZED_SOURCE_STREAM_H_
