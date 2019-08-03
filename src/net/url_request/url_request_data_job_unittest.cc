// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <string>

#include "base/memory/ref_counted.h"
#include "net/base/net_errors.h"
#include "net/http/http_response_headers.h"
#include "net/http/http_version.h"
#include "net/url_request/url_request_data_job.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "url/gurl.h"

namespace net {

TEST(BuildResponseTest, Simple) {
  std::string mime_type;
  std::string charset;
  std::string data;
  scoped_refptr<HttpResponseHeaders> headers(
      new HttpResponseHeaders(std::string()));

  ASSERT_EQ(OK,
            URLRequestDataJob::BuildResponse(GURL("data:,Hello"), &mime_type,
                                             &charset, &data, headers.get()));

  EXPECT_EQ("text/plain", mime_type);
  EXPECT_EQ("US-ASCII", charset);
  EXPECT_EQ("Hello", data);

  const HttpVersion& version = headers->GetHttpVersion();
  EXPECT_EQ(1, version.major_value());
  EXPECT_EQ(1, version.minor_value());
  EXPECT_EQ("OK", headers->GetStatusText());
  std::string value;
  EXPECT_TRUE(headers->GetNormalizedHeader("Content-Type", &value));
  EXPECT_EQ(value, "text/plain;charset=US-ASCII");
  value.clear();
}

TEST(BuildResponseTest, InvalidInput) {
  std::string mime_type;
  std::string charset;
  std::string data;
  scoped_refptr<HttpResponseHeaders> headers(
      new HttpResponseHeaders(std::string()));

  EXPECT_EQ(ERR_INVALID_URL,
            URLRequestDataJob::BuildResponse(GURL("bogus"), &mime_type,
                                             &charset, &data, headers.get()));
}

TEST(BuildResponseTest, InvalidMimeType) {
  std::string mime_type;
  std::string charset;
  std::string data;
  scoped_refptr<HttpResponseHeaders> headers(
      new HttpResponseHeaders(std::string()));

  // MIME type contains delimiters. Must be accepted but Content-Type header
  // should be generated as if the mediatype was text/plain.
  EXPECT_EQ(OK, URLRequestDataJob::BuildResponse(GURL("data:f(o/b)r,test"),
                                                 &mime_type, &charset, &data,
                                                 headers.get()));

  std::string value;
  EXPECT_TRUE(headers->GetNormalizedHeader("Content-Type", &value));
  EXPECT_EQ(value, "text/plain;charset=US-ASCII");
}

TEST(BuildResponseTest, InvalidCharset) {
  std::string mime_type;
  std::string charset;
  std::string data;
  scoped_refptr<HttpResponseHeaders> headers(
      new HttpResponseHeaders(std::string()));

  // MIME type contains delimiters. Must be rejected.
  EXPECT_EQ(ERR_INVALID_URL, URLRequestDataJob::BuildResponse(
                                 GURL("data:text/html;charset=(),test"),
                                 &mime_type, &charset, &data, headers.get()));
}

}  // namespace net
