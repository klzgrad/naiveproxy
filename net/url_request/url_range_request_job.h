// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef NET_URL_REQUEST_URL_RANGE_REQUEST_JOB_H_
#define NET_URL_REQUEST_URL_RANGE_REQUEST_JOB_H_

#include <vector>

#include "net/base/net_export.h"
#include "net/http/http_byte_range.h"
#include "net/url_request/url_request_job.h"

namespace net {

class HttpRequestHeaders;

// URLRequestJob with support for parsing range requests.
// It is up to subclasses to handle the response
// and deal with an errors parsing the range request header.
// This must be done after Start() has been called.
class NET_EXPORT URLRangeRequestJob : public URLRequestJob {
 public:
  URLRangeRequestJob(URLRequest* request,
                     NetworkDelegate* delegate);

  void SetExtraRequestHeaders(const HttpRequestHeaders& headers) override;

  const std::vector<HttpByteRange>& ranges() const { return ranges_; }
  int range_parse_result() const { return range_parse_result_; }

 protected:
  ~URLRangeRequestJob() override;

 private:
  std::vector<HttpByteRange> ranges_;
  int range_parse_result_;
};

}  // namespace net

#endif  // NET_URL_REQUEST_URL_RANGE_REQUEST_JOB_H_
