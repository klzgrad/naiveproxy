// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/log/net_log_capture_mode.h"

#include "testing/gtest/include/gtest/gtest.h"

namespace net {

namespace {

TEST(NetLogCaptureMode, DefaultConstructor) {
  EXPECT_EQ(NetLogCaptureMode(), NetLogCaptureMode::Default());
}

TEST(NetLogCaptureMode, Default) {
  NetLogCaptureMode mode = NetLogCaptureMode::Default();

  EXPECT_FALSE(mode.include_cookies_and_credentials());
  EXPECT_FALSE(mode.include_socket_bytes());

  EXPECT_EQ(mode, NetLogCaptureMode::Default());
  EXPECT_NE(mode, NetLogCaptureMode::IncludeCookiesAndCredentials());
  EXPECT_NE(mode, NetLogCaptureMode::IncludeSocketBytes());
}

TEST(NetLogCaptureMode, IncludeCookiesAndCredentials) {
  NetLogCaptureMode mode = NetLogCaptureMode::IncludeCookiesAndCredentials();

  EXPECT_TRUE(mode.include_cookies_and_credentials());
  EXPECT_FALSE(mode.include_socket_bytes());

  EXPECT_NE(mode, NetLogCaptureMode::Default());
  EXPECT_EQ(mode, NetLogCaptureMode::IncludeCookiesAndCredentials());
  EXPECT_NE(mode, NetLogCaptureMode::IncludeSocketBytes());
}

TEST(NetLogCaptureMode, IncludeSocketBytes) {
  NetLogCaptureMode mode = NetLogCaptureMode::IncludeSocketBytes();

  EXPECT_TRUE(mode.include_cookies_and_credentials());
  EXPECT_TRUE(mode.include_socket_bytes());

  EXPECT_NE(mode, NetLogCaptureMode::Default());
  EXPECT_NE(mode, NetLogCaptureMode::IncludeCookiesAndCredentials());
  EXPECT_EQ(mode, NetLogCaptureMode::IncludeSocketBytes());
}

}  // namespace

}  // namespace net
