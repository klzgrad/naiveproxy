// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/url_request/url_range_request_job.h"

#include "net/base/net_errors.h"
#include "net/http/http_request_headers.h"
#include "net/http/http_util.h"

namespace net {

URLRangeRequestJob::URLRangeRequestJob(URLRequest* request,
    NetworkDelegate* delegate)
    : URLRequestJob(request, delegate), range_parse_result_(OK) {
}

URLRangeRequestJob::~URLRangeRequestJob() {
}

void URLRangeRequestJob::SetExtraRequestHeaders(
    const HttpRequestHeaders& headers) {
  std::string range_header;
  if (headers.GetHeader(HttpRequestHeaders::kRange, &range_header)) {
    if (!HttpUtil::ParseRangeHeader(range_header, &ranges_)) {
      range_parse_result_ = ERR_REQUEST_RANGE_NOT_SATISFIABLE;
    }
  }
}

}  // namespace net
