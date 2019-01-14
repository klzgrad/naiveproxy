// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/spdy/spdy_log_util.h"

#include "base/values.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace net {

TEST(SpdyLogUtilTest, ElideGoAwayDebugDataForNetLog) {
  // Only elide for appropriate log level.
  EXPECT_EQ(
      "[6 bytes were stripped]",
      ElideGoAwayDebugDataForNetLog(NetLogCaptureMode::Default(), "foobar"));
  EXPECT_EQ("foobar",
            ElideGoAwayDebugDataForNetLog(
                NetLogCaptureMode::IncludeCookiesAndCredentials(), "foobar"));
}

TEST(SpdyLogUtilTest, ElideSpdyHeaderBlockForNetLog) {
  spdy::SpdyHeaderBlock headers;
  headers["foo"] = "bar";
  headers["cookie"] = "name=value";

  std::unique_ptr<base::ListValue> list =
      ElideSpdyHeaderBlockForNetLog(headers, NetLogCaptureMode::Default());
  EXPECT_EQ(2u, list->GetSize());
  std::string field;
  EXPECT_TRUE(list->GetString(0, &field));
  EXPECT_EQ("foo: bar", field);
  EXPECT_TRUE(list->GetString(1, &field));
  EXPECT_EQ("cookie: [10 bytes were stripped]", field);

  list = ElideSpdyHeaderBlockForNetLog(
      headers, NetLogCaptureMode::IncludeCookiesAndCredentials());
  EXPECT_EQ(2u, list->GetSize());
  EXPECT_TRUE(list->GetString(0, &field));
  EXPECT_EQ("foo: bar", field);
  EXPECT_TRUE(list->GetString(1, &field));
  EXPECT_EQ("cookie: name=value", field);
}

}  // namespace net
