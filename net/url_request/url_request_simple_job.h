// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef NET_URL_REQUEST_URL_REQUEST_SIMPLE_JOB_H_
#define NET_URL_REQUEST_URL_REQUEST_SIMPLE_JOB_H_

#include <stdint.h>

#include <string>

#include "base/memory/ref_counted.h"
#include "base/memory/weak_ptr.h"
#include "base/strings/string_piece.h"
#include "net/base/completion_callback.h"
#include "net/base/net_export.h"
#include "net/url_request/url_range_request_job.h"

namespace base {
class RefCountedMemory;
}

namespace net {

class URLRequest;

class NET_EXPORT URLRequestSimpleJob : public URLRangeRequestJob {
 public:
  URLRequestSimpleJob(URLRequest* request, NetworkDelegate* network_delegate);

  void Start() override;
  void Kill() override;
  int ReadRawData(IOBuffer* buf, int buf_size) override;
  bool GetMimeType(std::string* mime_type) const override;
  bool GetCharset(std::string* charset) override;

 protected:
  ~URLRequestSimpleJob() override;

  // Subclasses must override either GetData or GetRefCountedData to define the
  // way response data is determined.
  // The return value should be:
  //  - OK if data is obtained;
  //  - ERR_IO_PENDING if async processing is needed to finish obtaining data.
  //    This is the only case when |callback| should be called after
  //    completion of the operation. In other situations |callback| should
  //    never be called;
  //  - any other ERR_* code to indicate an error. This code will be used
  //    as the error code in the URLRequestStatus when the URLRequest
  //    is finished.
  virtual int GetData(std::string* mime_type,
                      std::string* charset,
                      std::string* data,
                      const CompletionCallback& callback) const;

  // Similar to GetData(), except |*data| can share ownership of the bytes
  // instead of copying them into a std::string.
  virtual int GetRefCountedData(std::string* mime_type,
                                std::string* charset,
                                scoped_refptr<base::RefCountedMemory>* data,
                                const CompletionCallback& callback) const;

  void StartAsync();

 private:
  void OnGetDataCompleted(int result);

  HttpByteRange byte_range_;
  std::string mime_type_;
  std::string charset_;
  scoped_refptr<base::RefCountedMemory> data_;
  int64_t next_data_offset_;
  base::WeakPtrFactory<URLRequestSimpleJob> weak_factory_;
};

}  // namespace net

#endif  // NET_URL_REQUEST_URL_REQUEST_SIMPLE_JOB_H_
