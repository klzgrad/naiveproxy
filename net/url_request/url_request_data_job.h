// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef NET_URL_REQUEST_URL_REQUEST_DATA_JOB_H_
#define NET_URL_REQUEST_URL_REQUEST_DATA_JOB_H_

#include <string>

#include "base/macros.h"
#include "net/base/net_export.h"
#include "net/url_request/url_request.h"
#include "net/url_request/url_request_simple_job.h"

class GURL;

namespace net {

class HttpResponseHeaders;
class URLRequest;

class NET_EXPORT URLRequestDataJob : public URLRequestSimpleJob {
 public:
  // Extracts info from a data scheme URL. Returns OK if successful. Returns
  // ERR_INVALID_URL otherwise.
  static int BuildResponse(const GURL& url,
                           std::string* mime_type,
                           std::string* charset,
                           std::string* data,
                           HttpResponseHeaders* headers);

  URLRequestDataJob(URLRequest* request, NetworkDelegate* network_delegate);

  // URLRequestSimpleJob
  int GetData(std::string* mime_type,
              std::string* charset,
              std::string* data,
              const CompletionCallback& callback) const override;

 private:
  ~URLRequestDataJob() override;

  DISALLOW_COPY_AND_ASSIGN(URLRequestDataJob);
};

}  // namespace net

#endif  // NET_URL_REQUEST_URL_REQUEST_DATA_JOB_H_
