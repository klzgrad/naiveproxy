// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/url_request/view_cache_helper.h"

#include <memory>

#include "base/pickle.h"
#include "base/test/scoped_task_environment.h"
#include "net/base/net_errors.h"
#include "net/base/request_priority.h"
#include "net/base/test_completion_callback.h"
#include "net/disk_cache/disk_cache.h"
#include "net/http/http_cache.h"
#include "net/http/http_transaction_test_util.h"
#include "net/test/gtest_util.h"
#include "net/url_request/url_request_context.h"
#include "testing/gmock/include/gmock/gmock.h"
#include "testing/gtest/include/gtest/gtest.h"

using net::test::IsOk;

namespace net {

namespace {

class TestURLRequestContext : public URLRequestContext {
 public:
  TestURLRequestContext();

  ~TestURLRequestContext() override { AssertNoURLRequests(); }

  // Gets a pointer to the cache backend.
  disk_cache::Backend* GetBackend();

 private:
  HttpCache cache_;
};

TestURLRequestContext::TestURLRequestContext()
    : cache_(std::make_unique<MockNetworkLayer>(),
             HttpCache::DefaultBackend::InMemory(0),
             false /* is_main_cache */) {
  set_http_transaction_factory(&cache_);
}

void WriteHeaders(disk_cache::Entry* entry, int flags,
                  const std::string& data) {
  if (data.empty())
    return;

  base::Pickle pickle;
  pickle.WriteInt(flags | 3);  // Version 3.
  pickle.WriteInt64(0);
  pickle.WriteInt64(0);
  pickle.WriteString(data);
  pickle.WriteString("example.com");
  pickle.WriteUInt16(80);

  scoped_refptr<WrappedIOBuffer> buf = base::MakeRefCounted<WrappedIOBuffer>(
      reinterpret_cast<const char*>(pickle.data()));
  int len = static_cast<int>(pickle.size());

  TestCompletionCallback cb;
  int rv = entry->WriteData(0, 0, buf.get(), len, cb.callback(), true);
  ASSERT_EQ(len, cb.GetResult(rv));
}

void WriteData(disk_cache::Entry* entry, int index, const std::string& data) {
  if (data.empty())
    return;

  int len = data.length();
  scoped_refptr<IOBuffer> buf = base::MakeRefCounted<IOBuffer>(len);
  memcpy(buf->data(), data.data(), data.length());

  TestCompletionCallback cb;
  int rv = entry->WriteData(index, 0, buf.get(), len, cb.callback(), true);
  ASSERT_EQ(len, cb.GetResult(rv));
}

void WriteToEntry(disk_cache::Backend* cache, const std::string& key,
                  const std::string& data0, const std::string& data1,
                  const std::string& data2) {
  TestCompletionCallback cb;
  disk_cache::Entry* entry;
  int rv = cache->CreateEntry(key, net::HIGHEST, &entry, cb.callback());
  rv = cb.GetResult(rv);
  if (rv != OK) {
    rv = cache->OpenEntry(key, net::HIGHEST, &entry, cb.callback());
    ASSERT_THAT(cb.GetResult(rv), IsOk());
  }

  WriteHeaders(entry, 0, data0);
  WriteData(entry, 1, data1);
  WriteData(entry, 2, data2);

  entry->Close();
}

void FillCache(URLRequestContext* context) {
  TestCompletionCallback cb;
  disk_cache::Backend* cache;
  int rv =
      context->http_transaction_factory()->GetCache()->GetBackend(
          &cache, cb.callback());
  ASSERT_THAT(cb.GetResult(rv), IsOk());

  std::string empty;
  WriteToEntry(cache, "first", "some", empty, empty);
  WriteToEntry(cache, "second", "only hex_dumped", "same", "kind");
  WriteToEntry(cache, "third", empty, "another", "thing");
}

}  // namespace.

TEST(ViewCacheHelper, EmptyCache) {
  base::test::ScopedTaskEnvironment scoped_task_environment;
  TestURLRequestContext context;
  ViewCacheHelper helper;

  TestCompletionCallback cb;
  std::string prefix, data;
  int rv = helper.GetContentsHTML(&context, prefix, &data, cb.callback());
  EXPECT_THAT(cb.GetResult(rv), IsOk());
  EXPECT_FALSE(data.empty());
}

TEST(ViewCacheHelper, ListContents) {
  base::test::ScopedTaskEnvironment scoped_task_environment;
  TestURLRequestContext context;
  ViewCacheHelper helper;

  FillCache(&context);

  std::string prefix, data;
  TestCompletionCallback cb;
  int rv = helper.GetContentsHTML(&context, prefix, &data, cb.callback());
  EXPECT_THAT(cb.GetResult(rv), IsOk());

  EXPECT_EQ(0U, data.find("<html>"));
  EXPECT_NE(std::string::npos, data.find("</html>"));
  EXPECT_NE(std::string::npos, data.find("first"));
  EXPECT_NE(std::string::npos, data.find("second"));
  EXPECT_NE(std::string::npos, data.find("third"));

  EXPECT_EQ(std::string::npos, data.find("some"));
  EXPECT_EQ(std::string::npos, data.find("same"));
  EXPECT_EQ(std::string::npos, data.find("thing"));
}

TEST(ViewCacheHelper, DumpEntry) {
  base::test::ScopedTaskEnvironment scoped_task_environment;
  TestURLRequestContext context;
  ViewCacheHelper helper;

  FillCache(&context);

  std::string data;
  TestCompletionCallback cb;
  int rv = helper.GetEntryInfoHTML("second", &context, &data, cb.callback());
  EXPECT_THAT(cb.GetResult(rv), IsOk());

  EXPECT_EQ(0U, data.find("<html>"));
  EXPECT_NE(std::string::npos, data.find("</html>"));

  EXPECT_NE(std::string::npos, data.find("hex_dumped"));
  EXPECT_NE(std::string::npos, data.find("same"));
  EXPECT_NE(std::string::npos, data.find("kind"));

  EXPECT_EQ(std::string::npos, data.find("first"));
  EXPECT_EQ(std::string::npos, data.find("third"));
  EXPECT_EQ(std::string::npos, data.find("some"));
  EXPECT_EQ(std::string::npos, data.find("another"));
}

// Makes sure the links are correct.
TEST(ViewCacheHelper, Prefix) {
  base::test::ScopedTaskEnvironment scoped_task_environment;
  TestURLRequestContext context;
  ViewCacheHelper helper;

  FillCache(&context);

  std::string key, data;
  std::string prefix("prefix:");
  TestCompletionCallback cb;
  int rv = helper.GetContentsHTML(&context, prefix, &data, cb.callback());
  EXPECT_THAT(cb.GetResult(rv), IsOk());

  EXPECT_EQ(0U, data.find("<html>"));
  EXPECT_NE(std::string::npos, data.find("</html>"));
  EXPECT_NE(std::string::npos, data.find("<a href=\"prefix:first\">"));
  EXPECT_NE(std::string::npos, data.find("<a href=\"prefix:second\">"));
  EXPECT_NE(std::string::npos, data.find("<a href=\"prefix:third\">"));
}

TEST(ViewCacheHelper, TruncatedFlag) {
  base::test::ScopedTaskEnvironment scoped_task_environment;
  TestURLRequestContext context;
  ViewCacheHelper helper;

  TestCompletionCallback cb;
  disk_cache::Backend* cache;
  int rv =
      context.http_transaction_factory()->GetCache()->GetBackend(
          &cache, cb.callback());
  ASSERT_THAT(cb.GetResult(rv), IsOk());

  std::string key("the key");
  disk_cache::Entry* entry;
  rv = cache->CreateEntry(key, net::HIGHEST, &entry, cb.callback());
  ASSERT_THAT(cb.GetResult(rv), IsOk());

  // RESPONSE_INFO_TRUNCATED defined on response_info.cc
  int flags = 1 << 12;
  WriteHeaders(entry, flags, "something");
  entry->Close();

  std::string data;
  TestCompletionCallback cb1;
  rv = helper.GetEntryInfoHTML(key, &context, &data, cb1.callback());
  EXPECT_THAT(cb1.GetResult(rv), IsOk());

  EXPECT_NE(std::string::npos, data.find("RESPONSE_INFO_TRUNCATED"));
}

}  // namespace net
