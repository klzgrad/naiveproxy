// Copyright (c) 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef NET_URL_REQUEST_URL_REQUEST_FILE_DIR_JOB_H_
#define NET_URL_REQUEST_URL_REQUEST_FILE_DIR_JOB_H_

#include <string>

#include "base/compiler_specific.h"
#include "base/files/file_path.h"
#include "base/macros.h"
#include "base/memory/weak_ptr.h"
#include "net/base/directory_lister.h"
#include "net/base/net_errors.h"
#include "net/base/net_export.h"
#include "net/url_request/url_request_job.h"

namespace net {

class NET_EXPORT_PRIVATE URLRequestFileDirJob
    : public URLRequestJob,
      public DirectoryLister::DirectoryListerDelegate {
 public:
  URLRequestFileDirJob(URLRequest* request,
                       NetworkDelegate* network_delegate,
                       const base::FilePath& dir_path);

  void StartAsync();

  // Overridden from URLRequestJob:
  void Start() override;
  void Kill() override;
  int ReadRawData(IOBuffer* buf, int buf_size) override;
  bool GetMimeType(std::string* mime_type) const override;
  bool GetCharset(std::string* charset) override;

  // Overridden from DirectoryLister::DirectoryListerDelegate:
  void OnListFile(const DirectoryLister::DirectoryListerData& data) override;
  void OnListDone(int error) override;

 protected:
  ~URLRequestFileDirJob() override;

 private:
  // Called after the target directory path is resolved to an absolute path.
  void DidMakeAbsolutePath(const base::FilePath& absolute_path);

  // When we have data and a read has been pending, this function
  // will fill the response buffer and notify the request
  // appropriately.
  void CompleteRead(Error error);

  int ReadBuffer(char* buf, int buf_size);

  DirectoryLister lister_;
  const base::FilePath dir_path_;

  std::string data_;
  bool canceled_;

  // Indicates whether we have the complete list of the dir
  bool list_complete_;

  // Indicates the status of the list
  Error list_complete_result_;

  // Indicates whether we have written the HTML header
  bool wrote_header_;

  // To simulate Async IO, we hold onto the Reader's buffer while
  // we wait for IO to complete.  When done, we fill the buffer
  // manually.
  bool read_pending_;
  scoped_refptr<IOBuffer> read_buffer_;
  int read_buffer_length_;

  base::WeakPtrFactory<URLRequestFileDirJob> weak_factory_;

  DISALLOW_COPY_AND_ASSIGN(URLRequestFileDirJob);
};

}  // namespace net

#endif  // NET_URL_REQUEST_URL_REQUEST_FILE_DIR_JOB_H_
