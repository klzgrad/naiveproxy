// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Simple implementation of a data: protocol handler.

#include "net/url_request/url_request_data_job.h"

#include "net/base/data_url.h"
#include "net/base/net_errors.h"
#include "net/http/http_response_headers.h"
#include "url/gurl.h"

namespace net {

int URLRequestDataJob::BuildResponse(const GURL& url,
                                     std::string* mime_type,
                                     std::string* charset,
                                     std::string* data,
                                     HttpResponseHeaders* headers) {
  if (!DataURL::Parse(url, mime_type, charset, data))
    return ERR_INVALID_URL;

  // |mime_type| set by DataURL::Parse() is guaranteed to be in
  //     token "/" token
  // form. |charset| can be an empty string.

  DCHECK(!mime_type->empty());

  if (headers) {
    headers->ReplaceStatusLine("HTTP/1.1 200 OK");
    // "charset" in the Content-Type header is specified explicitly to follow
    // the "token" ABNF in the HTTP spec. When DataURL::Parse() call is
    // successful, it's guaranteed that the string in |charset| follows the
    // "token" ABNF.
    std::string content_type_header = "Content-Type: " + *mime_type;
    if (!charset->empty())
      content_type_header.append(";charset=" + *charset);
    headers->AddHeader(content_type_header);
    headers->AddHeader("Access-Control-Allow-Origin: *");
  }

  return OK;
}

URLRequestDataJob::URLRequestDataJob(
    URLRequest* request, NetworkDelegate* network_delegate)
    : URLRequestSimpleJob(request, network_delegate) {
}

int URLRequestDataJob::GetData(std::string* mime_type,
                               std::string* charset,
                               std::string* data,
                               const CompletionCallback& callback) const {
  // Check if data URL is valid. If not, don't bother to try to extract data.
  // Otherwise, parse the data from the data URL.
  const GURL& url = request_->url();
  if (!url.is_valid())
    return ERR_INVALID_URL;

  // TODO(tyoshino): Get the headers and export via
  // URLRequestJob::GetResponseInfo().
  return BuildResponse(url, mime_type, charset, data, NULL);
}

URLRequestDataJob::~URLRequestDataJob() {
}

}  // namespace net
