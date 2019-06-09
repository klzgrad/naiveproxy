// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/url_request/file_protocol_handler.h"

#include "base/logging.h"
#include "base/task_runner.h"
#include "net/base/filename_util.h"
#include "net/base/net_errors.h"
#include "net/url_request/url_request.h"
#include "net/url_request/url_request_error_job.h"
#include "net/url_request/url_request_file_dir_job.h"
#include "net/url_request/url_request_file_job.h"

namespace net {

FileProtocolHandler::FileProtocolHandler(
    const scoped_refptr<base::TaskRunner>& file_task_runner)
    : file_task_runner_(file_task_runner) {}

FileProtocolHandler::~FileProtocolHandler() = default;

URLRequestJob* FileProtocolHandler::MaybeCreateJob(
    URLRequest* request, NetworkDelegate* network_delegate) const {
  base::FilePath file_path;
  const bool is_file = FileURLToFilePath(request->url(), &file_path);

  // We need to decide whether to create URLRequestFileJob for file access or
  // URLRequestFileDirJob for directory access. To avoid accessing the
  // filesystem, we only look at the path string here.
  // The code in the URLRequestFileJob::Start() method discovers that a path,
  // which doesn't end with a slash, should really be treated as a directory,
  // and it then redirects to the URLRequestFileDirJob.
  if (is_file &&
      file_path.EndsWithSeparator() &&
      file_path.IsAbsolute()) {
    return new URLRequestFileDirJob(request, network_delegate, file_path);
  }

  // Use a regular file request job for all non-directories (including invalid
  // file names).
  return new URLRequestFileJob(request, network_delegate, file_path,
                               file_task_runner_);
}

bool FileProtocolHandler::IsSafeRedirectTarget(const GURL& location) const {
  return false;
}

}  // namespace net
