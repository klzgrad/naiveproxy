// Copyright (c) 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/third_party/quic/platform/api/quic_hostname_utils.h"

#include "net/third_party/quic/platform/api/quic_arraysize.h"
#include "net/third_party/quic/platform/api/quic_string.h"
#include "net/third_party/quic/platform/api/quic_test.h"

namespace quic {
namespace test {
namespace {

class QuicHostnameUtilsTest : public QuicTest {};

TEST_F(QuicHostnameUtilsTest, IsValidSNI) {
  // IP as SNI.
  EXPECT_FALSE(QuicHostnameUtils::IsValidSNI("192.168.0.1"));
  // SNI without any dot.
  EXPECT_FALSE(QuicHostnameUtils::IsValidSNI("somedomain"));
  // Invalid by RFC2396 but unfortunately domains of this form exist.
  EXPECT_TRUE(QuicHostnameUtils::IsValidSNI("some_domain.com"));
  // An empty string must be invalid otherwise the QUIC client will try sending
  // it.
  EXPECT_FALSE(QuicHostnameUtils::IsValidSNI(""));

  // Valid SNI
  EXPECT_TRUE(QuicHostnameUtils::IsValidSNI("test.google.com"));
}

TEST_F(QuicHostnameUtilsTest, NormalizeHostname) {
  // clang-format off
  struct {
    const char *input, *expected;
  } tests[] = {
      {
          "www.google.com",
          "www.google.com",
      },
      {
          "WWW.GOOGLE.COM",
          "www.google.com",
      },
      {
          "www.google.com.",
          "www.google.com",
      },
      {
          "www.google.COM.",
          "www.google.com",
      },
      {
          "www.google.com..",
          "www.google.com",
      },
      {
          "www.google.com........",
          "www.google.com",
      },
  };
  // clang-format on

  for (size_t i = 0; i < QUIC_ARRAYSIZE(tests); ++i) {
    char buf[256];
    snprintf(buf, sizeof(buf), "%s", tests[i].input);
    EXPECT_EQ(QuicString(tests[i].expected),
              QuicHostnameUtils::NormalizeHostname(buf));
  }
}

}  // namespace
}  // namespace test
}  // namespace quic
