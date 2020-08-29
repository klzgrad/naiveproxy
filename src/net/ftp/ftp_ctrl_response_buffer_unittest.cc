// Copyright (c) 2009 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/ftp/ftp_ctrl_response_buffer.h"

#include <string.h>

#include "net/base/net_errors.h"
#include "net/test/gtest_util.h"
#include "testing/gmock/include/gmock/gmock.h"
#include "testing/gtest/include/gtest/gtest.h"

using net::test::IsError;
using net::test::IsOk;

namespace net {

namespace {

class FtpCtrlResponseBufferTest : public testing::Test {
 public:
  FtpCtrlResponseBufferTest() : buffer_(NetLogWithSource()) {}

 protected:
  int PushDataToBuffer(const char* data) {
    return buffer_.ConsumeData(data, strlen(data));
  }

  FtpCtrlResponseBuffer buffer_;
};

TEST_F(FtpCtrlResponseBufferTest, Basic) {
  EXPECT_FALSE(buffer_.ResponseAvailable());

  EXPECT_THAT(PushDataToBuffer("200 Status Text\r\n"), IsOk());
  EXPECT_TRUE(buffer_.ResponseAvailable());

  FtpCtrlResponse response = buffer_.PopResponse();
  EXPECT_FALSE(buffer_.ResponseAvailable());
  EXPECT_EQ(200, response.status_code);
  ASSERT_EQ(1U, response.lines.size());
  EXPECT_EQ("Status Text", response.lines[0]);
}

TEST_F(FtpCtrlResponseBufferTest, Chunks) {
  EXPECT_THAT(PushDataToBuffer("20"), IsOk());
  EXPECT_FALSE(buffer_.ResponseAvailable());
  EXPECT_THAT(PushDataToBuffer("0 Status"), IsOk());
  EXPECT_FALSE(buffer_.ResponseAvailable());
  EXPECT_THAT(PushDataToBuffer(" Text"), IsOk());
  EXPECT_FALSE(buffer_.ResponseAvailable());
  EXPECT_THAT(PushDataToBuffer("\r"), IsOk());
  EXPECT_FALSE(buffer_.ResponseAvailable());
  EXPECT_THAT(PushDataToBuffer("\n"), IsOk());
  EXPECT_TRUE(buffer_.ResponseAvailable());

  FtpCtrlResponse response = buffer_.PopResponse();
  EXPECT_FALSE(buffer_.ResponseAvailable());
  EXPECT_EQ(200, response.status_code);
  ASSERT_EQ(1U, response.lines.size());
  EXPECT_EQ("Status Text", response.lines[0]);
}

TEST_F(FtpCtrlResponseBufferTest, Continuation) {
  EXPECT_THAT(PushDataToBuffer("230-FirstLine\r\n"), IsOk());
  EXPECT_FALSE(buffer_.ResponseAvailable());

  EXPECT_THAT(PushDataToBuffer("230-SecondLine\r\n"), IsOk());
  EXPECT_FALSE(buffer_.ResponseAvailable());

  EXPECT_THAT(PushDataToBuffer("230 LastLine\r\n"), IsOk());
  EXPECT_TRUE(buffer_.ResponseAvailable());

  FtpCtrlResponse response = buffer_.PopResponse();
  EXPECT_FALSE(buffer_.ResponseAvailable());
  EXPECT_EQ(230, response.status_code);
  ASSERT_EQ(3U, response.lines.size());
  EXPECT_EQ("FirstLine", response.lines[0]);
  EXPECT_EQ("SecondLine", response.lines[1]);
  EXPECT_EQ("LastLine", response.lines[2]);
}

TEST_F(FtpCtrlResponseBufferTest, MultilineContinuation) {
  EXPECT_THAT(PushDataToBuffer("230-FirstLine\r\n"), IsOk());
  EXPECT_FALSE(buffer_.ResponseAvailable());

  EXPECT_THAT(PushDataToBuffer("Continued\r\n"), IsOk());
  EXPECT_FALSE(buffer_.ResponseAvailable());

  EXPECT_THAT(PushDataToBuffer("230-SecondLine\r\n"), IsOk());
  EXPECT_FALSE(buffer_.ResponseAvailable());

  EXPECT_THAT(PushDataToBuffer("215 Continued\r\n"), IsOk());
  EXPECT_FALSE(buffer_.ResponseAvailable());

  EXPECT_THAT(PushDataToBuffer("230 LastLine\r\n"), IsOk());
  EXPECT_TRUE(buffer_.ResponseAvailable());

  FtpCtrlResponse response = buffer_.PopResponse();
  EXPECT_FALSE(buffer_.ResponseAvailable());
  EXPECT_EQ(230, response.status_code);
  ASSERT_EQ(3U, response.lines.size());
  EXPECT_EQ("FirstLineContinued", response.lines[0]);
  EXPECT_EQ("SecondLine215 Continued", response.lines[1]);
  EXPECT_EQ("LastLine", response.lines[2]);
}

TEST_F(FtpCtrlResponseBufferTest, MultilineContinuationZeroLength) {
  // For the corner case from bug 29322.
  EXPECT_THAT(PushDataToBuffer("230-\r\n"), IsOk());
  EXPECT_FALSE(buffer_.ResponseAvailable());

  EXPECT_THAT(PushDataToBuffer("example.com\r\n"), IsOk());
  EXPECT_FALSE(buffer_.ResponseAvailable());

  EXPECT_THAT(PushDataToBuffer("230 LastLine\r\n"), IsOk());
  EXPECT_TRUE(buffer_.ResponseAvailable());

  FtpCtrlResponse response = buffer_.PopResponse();
  EXPECT_FALSE(buffer_.ResponseAvailable());
  EXPECT_EQ(230, response.status_code);
  ASSERT_EQ(2U, response.lines.size());
  EXPECT_EQ("example.com", response.lines[0]);
  EXPECT_EQ("LastLine", response.lines[1]);
}

TEST_F(FtpCtrlResponseBufferTest, SimilarContinuation) {
  EXPECT_THAT(PushDataToBuffer("230-FirstLine\r\n"), IsOk());
  EXPECT_FALSE(buffer_.ResponseAvailable());

  // Notice the space at the start of the line. It should be recognized
  // as a continuation, and not the last line.
  EXPECT_THAT(PushDataToBuffer(" 230 Continued\r\n"), IsOk());
  EXPECT_FALSE(buffer_.ResponseAvailable());

  EXPECT_THAT(PushDataToBuffer("230 TrueLastLine\r\n"), IsOk());
  EXPECT_TRUE(buffer_.ResponseAvailable());

  FtpCtrlResponse response = buffer_.PopResponse();
  EXPECT_FALSE(buffer_.ResponseAvailable());
  EXPECT_EQ(230, response.status_code);
  ASSERT_EQ(2U, response.lines.size());
  EXPECT_EQ("FirstLine 230 Continued", response.lines[0]);
  EXPECT_EQ("TrueLastLine", response.lines[1]);
}

// The nesting of multi-line responses is not allowed.
TEST_F(FtpCtrlResponseBufferTest, NoNesting) {
  EXPECT_THAT(PushDataToBuffer("230-FirstLine\r\n"), IsOk());
  EXPECT_FALSE(buffer_.ResponseAvailable());

  EXPECT_THAT(PushDataToBuffer("300-Continuation\r\n"), IsOk());
  EXPECT_FALSE(buffer_.ResponseAvailable());

  EXPECT_THAT(PushDataToBuffer("300 Still continuation\r\n"), IsOk());
  EXPECT_FALSE(buffer_.ResponseAvailable());

  EXPECT_THAT(PushDataToBuffer("230 Real End\r\n"), IsOk());
  ASSERT_TRUE(buffer_.ResponseAvailable());

  FtpCtrlResponse response = buffer_.PopResponse();
  EXPECT_FALSE(buffer_.ResponseAvailable());
  EXPECT_EQ(230, response.status_code);
  ASSERT_EQ(2U, response.lines.size());
  EXPECT_EQ("FirstLine300-Continuation300 Still continuation",
            response.lines[0]);
  EXPECT_EQ("Real End", response.lines[1]);
}

TEST_F(FtpCtrlResponseBufferTest, NonNumericResponse) {
  EXPECT_THAT(PushDataToBuffer("Non-numeric\r\n"),
              IsError(ERR_INVALID_RESPONSE));
  EXPECT_FALSE(buffer_.ResponseAvailable());
}

TEST_F(FtpCtrlResponseBufferTest, OutOfRangeResponse) {
  EXPECT_THAT(PushDataToBuffer("777 OK?\r\n"), IsError(ERR_INVALID_RESPONSE));
  EXPECT_FALSE(buffer_.ResponseAvailable());
}

}  // namespace

}  // namespace net
