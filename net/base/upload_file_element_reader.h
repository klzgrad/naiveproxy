// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef NET_BASE_UPLOAD_FILE_ELEMENT_READER_H_
#define NET_BASE_UPLOAD_FILE_ELEMENT_READER_H_

#include <stdint.h>

#include <memory>

#include "base/compiler_specific.h"
#include "base/files/file.h"
#include "base/files/file_path.h"
#include "base/gtest_prod_util.h"
#include "base/macros.h"
#include "base/memory/ref_counted.h"
#include "base/memory/weak_ptr.h"
#include "base/time/time.h"
#include "net/base/net_export.h"
#include "net/base/upload_element_reader.h"

namespace base {
class TaskRunner;
}

namespace net {

class FileStream;

// An UploadElementReader implementation for file.
class NET_EXPORT UploadFileElementReader : public UploadElementReader {
 public:
  // |task_runner| is used to perform file operations. It must not be NULL.
  UploadFileElementReader(base::TaskRunner* task_runner,
                          const base::FilePath& path,
                          uint64_t range_offset,
                          uint64_t range_length,
                          const base::Time& expected_modification_time);
  ~UploadFileElementReader() override;

  const base::FilePath& path() const { return path_; }
  uint64_t range_offset() const { return range_offset_; }
  uint64_t range_length() const { return range_length_; }
  const base::Time& expected_modification_time() const {
    return expected_modification_time_;
  }

  // UploadElementReader overrides:
  const UploadFileElementReader* AsFileReader() const override;
  int Init(const CompletionCallback& callback) override;
  uint64_t GetContentLength() const override;
  uint64_t BytesRemaining() const override;
  int Read(IOBuffer* buf,
           int buf_length,
           const CompletionCallback& callback) override;

 private:
  FRIEND_TEST_ALL_PREFIXES(ElementsUploadDataStreamTest, FileSmallerThanLength);
  FRIEND_TEST_ALL_PREFIXES(HttpNetworkTransactionTest,
                           UploadFileSmallerThanLength);

  // Resets this instance to the uninitialized state.
  void Reset();

  // These methods are used to implement Init().
  void OnOpenCompleted(const CompletionCallback& callback, int result);
  void OnSeekCompleted(const CompletionCallback& callback, int64_t result);
  void OnGetFileInfoCompleted(const CompletionCallback& callback,
                              base::File::Info* file_info,
                              bool result);

  // This method is used to implement Read().
  int OnReadCompleted(const CompletionCallback& callback, int result);

  // Sets an value to override the result for GetContentLength().
  // Used for tests.
  struct NET_EXPORT_PRIVATE ScopedOverridingContentLengthForTests {
    explicit ScopedOverridingContentLengthForTests(uint64_t value);
    ~ScopedOverridingContentLengthForTests();
  };

  scoped_refptr<base::TaskRunner> task_runner_;
  const base::FilePath path_;
  const uint64_t range_offset_;
  const uint64_t range_length_;
  const base::Time expected_modification_time_;
  std::unique_ptr<FileStream> file_stream_;
  uint64_t content_length_;
  uint64_t bytes_remaining_;
  base::WeakPtrFactory<UploadFileElementReader> weak_ptr_factory_;

  DISALLOW_COPY_AND_ASSIGN(UploadFileElementReader);
};

}  // namespace net

#endif  // NET_BASE_UPLOAD_FILE_ELEMENT_READER_H_
