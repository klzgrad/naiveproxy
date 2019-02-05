// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/proxy_resolution/dhcp_pac_file_fetcher_factory.h"
#include "base/test/scoped_task_environment.h"
#include "net/proxy_resolution/dhcp_pac_file_fetcher.h"
#include "net/url_request/url_request_test_util.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace net {
namespace {

#if defined(OS_WIN)
TEST(DhcpPacFileFetcherFactoryTest, WindowsFetcherOnWindows) {
  base::test::ScopedTaskEnvironment scoped_task_environment;
  DhcpPacFileFetcherFactory factory;
  TestURLRequestContext context;
  std::unique_ptr<DhcpPacFileFetcher> fetcher(factory.Create(&context));
  ASSERT_TRUE(fetcher.get());
  EXPECT_EQ("win", fetcher->GetFetcherName());
}

#else  // !defined(OS_WIN)

TEST(DhcpPacFileFetcherFactoryTest, ReturnNullOnUnsupportedPlatforms) {
  base::test::ScopedTaskEnvironment scoped_task_environment;
  DhcpPacFileFetcherFactory factory;
  TestURLRequestContext context;
  std::unique_ptr<DhcpPacFileFetcher> fetcher(factory.Create(&context));
  ASSERT_TRUE(fetcher.get());
  EXPECT_EQ("do nothing", fetcher->GetFetcherName());
}

#endif  // defined(OS_WIN)

}  // namespace
}  // namespace net
