// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef NET_URL_REQUEST_FILE_PROTOCOL_HANDLER_H_
#define NET_URL_REQUEST_FILE_PROTOCOL_HANDLER_H_

#include "base/compiler_specific.h"
#include "base/macros.h"
#include "base/memory/ref_counted.h"
#include "net/base/net_export.h"
#include "net/url_request/url_request_job_factory.h"

class GURL;

namespace base {
class TaskRunner;
}

namespace net {

class NetworkDelegate;
class URLRequestJob;

// Implements a ProtocolHandler for File jobs. If |network_delegate_| is NULL,
// then all file requests will fail with ERR_ACCESS_DENIED.
class NET_EXPORT FileProtocolHandler :
    public URLRequestJobFactory::ProtocolHandler {
 public:
  explicit FileProtocolHandler(
      const scoped_refptr<base::TaskRunner>& file_task_runner);
  ~FileProtocolHandler() override;
  URLRequestJob* MaybeCreateJob(
      URLRequest* request,
      NetworkDelegate* network_delegate) const override;
  bool IsSafeRedirectTarget(const GURL& location) const override;

 private:
  const scoped_refptr<base::TaskRunner> file_task_runner_;
  DISALLOW_COPY_AND_ASSIGN(FileProtocolHandler);
};

}  // namespace net

#endif  // NET_URL_REQUEST_FILE_PROTOCOL_HANDLER_H_
