// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/third_party/quic/tools/quic_memory_cache_backend.h"
#include "base/files/file_path.h"
#include "base/path_service.h"
#include "net/third_party/quic/platform/api/quic_map_util.h"
#include "net/third_party/quic/platform/api/quic_str_cat.h"
#include "net/third_party/quic/platform/api/quic_test.h"
#include "net/third_party/quic/platform/api/quic_text_utils.h"
#include "net/third_party/quic/tools/quic_backend_response.h"

namespace quic {
namespace test {

namespace {
typedef QuicBackendResponse Response;
typedef QuicBackendResponse::ServerPushInfo ServerPushInfo;
};  // namespace

class QuicMemoryCacheBackendTest : public QuicTest {
 protected:
  void CreateRequest(QuicString host,
                     QuicString path,
                     spdy::SpdyHeaderBlock* headers) {
    (*headers)[":method"] = "GET";
    (*headers)[":path"] = path;
    (*headers)[":authority"] = host;
    (*headers)[":scheme"] = "https";
  }

  QuicString CacheDirectory() {
    base::FilePath path;
    base::PathService::Get(base::DIR_SOURCE_ROOT, &path);
    path = path.AppendASCII("net").AppendASCII("data").AppendASCII(
        "quic_http_response_cache_data");
    // The file path is known to be an ascii string.
    return path.MaybeAsASCII();
  }

  QuicMemoryCacheBackend cache_;
};

TEST_F(QuicMemoryCacheBackendTest, GetResponseNoMatch) {
  const Response* response =
      cache_.GetResponse("mail.google.com", "/index.html");
  ASSERT_FALSE(response);
}

TEST_F(QuicMemoryCacheBackendTest, AddSimpleResponseGetResponse) {
  QuicString response_body("hello response");
  cache_.AddSimpleResponse("www.google.com", "/", 200, response_body);

  spdy::SpdyHeaderBlock request_headers;
  CreateRequest("www.google.com", "/", &request_headers);
  const Response* response = cache_.GetResponse("www.google.com", "/");
  ASSERT_TRUE(response);
  ASSERT_TRUE(QuicContainsKey(response->headers(), ":status"));
  EXPECT_EQ("200", response->headers().find(":status")->second);
  EXPECT_EQ(response_body.size(), response->body().length());
}

TEST_F(QuicMemoryCacheBackendTest, AddResponse) {
  const QuicString kRequestHost = "www.foo.com";
  const QuicString kRequestPath = "/";
  const QuicString kResponseBody("hello response");

  spdy::SpdyHeaderBlock response_headers;
  response_headers[":version"] = "HTTP/1.1";
  response_headers[":status"] = "200";
  response_headers["content-length"] =
      QuicTextUtils::Uint64ToString(kResponseBody.size());

  spdy::SpdyHeaderBlock response_trailers;
  response_trailers["key-1"] = "value-1";
  response_trailers["key-2"] = "value-2";
  response_trailers["key-3"] = "value-3";

  cache_.AddResponse(kRequestHost, "/", response_headers.Clone(), kResponseBody,
                     response_trailers.Clone());

  const Response* response = cache_.GetResponse(kRequestHost, kRequestPath);
  EXPECT_EQ(response->headers(), response_headers);
  EXPECT_EQ(response->body(), kResponseBody);
  EXPECT_EQ(response->trailers(), response_trailers);
}

TEST_F(QuicMemoryCacheBackendTest, ReadsCacheDir) {
  cache_.InitializeBackend(CacheDirectory());
  const Response* response =
      cache_.GetResponse("test.example.com", "/index.html");
  ASSERT_TRUE(response);
  ASSERT_TRUE(QuicContainsKey(response->headers(), ":status"));
  EXPECT_EQ("200", response->headers().find(":status")->second);
  // Connection headers are not valid in HTTP/2.
  EXPECT_FALSE(QuicContainsKey(response->headers(), "connection"));
  EXPECT_LT(0U, response->body().length());
}

TEST_F(QuicMemoryCacheBackendTest, ReadsCacheDirWithServerPushResource) {
  cache_.InitializeBackend(CacheDirectory() + "_with_push");
  std::list<ServerPushInfo> resources =
      cache_.GetServerPushResources("test.example.com/");
  ASSERT_EQ(1UL, resources.size());
}

TEST_F(QuicMemoryCacheBackendTest, ReadsCacheDirWithServerPushResources) {
  cache_.InitializeBackend(CacheDirectory() + "_with_push");
  std::list<ServerPushInfo> resources =
      cache_.GetServerPushResources("test.example.com/index2.html");
  ASSERT_EQ(2UL, resources.size());
}

TEST_F(QuicMemoryCacheBackendTest, UsesOriginalUrl) {
  cache_.InitializeBackend(CacheDirectory());
  const Response* response =
      cache_.GetResponse("test.example.com", "/site_map.html");
  ASSERT_TRUE(response);
  ASSERT_TRUE(QuicContainsKey(response->headers(), ":status"));
  EXPECT_EQ("200", response->headers().find(":status")->second);
  // Connection headers are not valid in HTTP/2.
  EXPECT_FALSE(QuicContainsKey(response->headers(), "connection"));
  EXPECT_LT(0U, response->body().length());
}

TEST_F(QuicMemoryCacheBackendTest, DefaultResponse) {
  // Verify GetResponse returns nullptr when no default is set.
  const Response* response = cache_.GetResponse("www.google.com", "/");
  ASSERT_FALSE(response);

  // Add a default response.
  spdy::SpdyHeaderBlock response_headers;
  response_headers[":version"] = "HTTP/1.1";
  response_headers[":status"] = "200";
  response_headers["content-length"] = "0";
  Response* default_response = new Response;
  default_response->set_headers(std::move(response_headers));
  cache_.AddDefaultResponse(default_response);

  // Now we should get the default response for the original request.
  response = cache_.GetResponse("www.google.com", "/");
  ASSERT_TRUE(response);
  ASSERT_TRUE(QuicContainsKey(response->headers(), ":status"));
  EXPECT_EQ("200", response->headers().find(":status")->second);

  // Now add a set response for / and make sure it is returned
  cache_.AddSimpleResponse("www.google.com", "/", 302, "");
  response = cache_.GetResponse("www.google.com", "/");
  ASSERT_TRUE(response);
  ASSERT_TRUE(QuicContainsKey(response->headers(), ":status"));
  EXPECT_EQ("302", response->headers().find(":status")->second);

  // We should get the default response for other requests.
  response = cache_.GetResponse("www.google.com", "/asd");
  ASSERT_TRUE(response);
  ASSERT_TRUE(QuicContainsKey(response->headers(), ":status"));
  EXPECT_EQ("200", response->headers().find(":status")->second);
}

TEST_F(QuicMemoryCacheBackendTest, AddSimpleResponseWithServerPushResources) {
  QuicString request_host = "www.foo.com";
  QuicString response_body("hello response");
  const size_t kNumResources = 5;
  int NumResources = 5;
  std::list<ServerPushInfo> push_resources;
  QuicString scheme = "http";
  for (int i = 0; i < NumResources; ++i) {
    QuicString path = "/server_push_src" + QuicTextUtils::Uint64ToString(i);
    QuicString url = scheme + "://" + request_host + path;
    QuicUrl resource_url(url);
    QuicString body =
        QuicStrCat("This is server push response body for ", path);
    spdy::SpdyHeaderBlock response_headers;
    response_headers[":version"] = "HTTP/1.1";
    response_headers[":status"] = "200";
    response_headers["content-length"] =
        QuicTextUtils::Uint64ToString(body.size());
    push_resources.push_back(
        ServerPushInfo(resource_url, response_headers.Clone(), i, body));
  }

  cache_.AddSimpleResponseWithServerPushResources(
      request_host, "/", 200, response_body, push_resources);

  QuicString request_url = request_host + "/";
  std::list<ServerPushInfo> resources =
      cache_.GetServerPushResources(request_url);
  ASSERT_EQ(kNumResources, resources.size());
  for (const auto& push_resource : push_resources) {
    ServerPushInfo resource = resources.front();
    EXPECT_EQ(resource.request_url.ToString(),
              push_resource.request_url.ToString());
    EXPECT_EQ(resource.priority, push_resource.priority);
    resources.pop_front();
  }
}

TEST_F(QuicMemoryCacheBackendTest, GetServerPushResourcesAndPushResponses) {
  QuicString request_host = "www.foo.com";
  QuicString response_body("hello response");
  const size_t kNumResources = 4;
  int NumResources = 4;
  QuicString scheme = "http";
  QuicString push_response_status[kNumResources] = {"200", "200", "301", "404"};
  std::list<ServerPushInfo> push_resources;
  for (int i = 0; i < NumResources; ++i) {
    QuicString path = "/server_push_src" + QuicTextUtils::Uint64ToString(i);
    QuicString url = scheme + "://" + request_host + path;
    QuicUrl resource_url(url);
    QuicString body = "This is server push response body for " + path;
    spdy::SpdyHeaderBlock response_headers;
    response_headers[":version"] = "HTTP/1.1";
    response_headers[":status"] = push_response_status[i];
    response_headers["content-length"] =
        QuicTextUtils::Uint64ToString(body.size());
    push_resources.push_back(
        ServerPushInfo(resource_url, response_headers.Clone(), i, body));
  }
  cache_.AddSimpleResponseWithServerPushResources(
      request_host, "/", 200, response_body, push_resources);
  QuicString request_url = request_host + "/";
  std::list<ServerPushInfo> resources =
      cache_.GetServerPushResources(request_url);
  ASSERT_EQ(kNumResources, resources.size());
  int i = 0;
  for (const auto& push_resource : push_resources) {
    QuicUrl url = resources.front().request_url;
    QuicString host = url.host();
    QuicString path = url.path();
    const Response* response = cache_.GetResponse(host, path);
    ASSERT_TRUE(response);
    ASSERT_TRUE(QuicContainsKey(response->headers(), ":status"));
    EXPECT_EQ(push_response_status[i++],
              response->headers().find(":status")->second);
    EXPECT_EQ(push_resource.body, response->body());
    resources.pop_front();
  }
}

}  // namespace test
}  // namespace quic
