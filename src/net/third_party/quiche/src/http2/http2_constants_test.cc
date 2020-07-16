// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/third_party/quiche/src/http2/http2_constants.h"

#include "net/third_party/quiche/src/http2/platform/api/http2_test_helpers.h"

namespace http2 {
namespace test {
namespace {

class Http2ConstantsTest : public testing::Test {};

TEST(Http2ConstantsTest, Http2FrameType) {
  EXPECT_EQ(Http2FrameType::DATA, static_cast<Http2FrameType>(0));
  EXPECT_EQ(Http2FrameType::HEADERS, static_cast<Http2FrameType>(1));
  EXPECT_EQ(Http2FrameType::PRIORITY, static_cast<Http2FrameType>(2));
  EXPECT_EQ(Http2FrameType::RST_STREAM, static_cast<Http2FrameType>(3));
  EXPECT_EQ(Http2FrameType::SETTINGS, static_cast<Http2FrameType>(4));
  EXPECT_EQ(Http2FrameType::PUSH_PROMISE, static_cast<Http2FrameType>(5));
  EXPECT_EQ(Http2FrameType::PING, static_cast<Http2FrameType>(6));
  EXPECT_EQ(Http2FrameType::GOAWAY, static_cast<Http2FrameType>(7));
  EXPECT_EQ(Http2FrameType::WINDOW_UPDATE, static_cast<Http2FrameType>(8));
  EXPECT_EQ(Http2FrameType::CONTINUATION, static_cast<Http2FrameType>(9));
  EXPECT_EQ(Http2FrameType::ALTSVC, static_cast<Http2FrameType>(10));
}

TEST(Http2ConstantsTest, Http2FrameTypeToString) {
  EXPECT_EQ("DATA", Http2FrameTypeToString(Http2FrameType::DATA));
  EXPECT_EQ("HEADERS", Http2FrameTypeToString(Http2FrameType::HEADERS));
  EXPECT_EQ("PRIORITY", Http2FrameTypeToString(Http2FrameType::PRIORITY));
  EXPECT_EQ("RST_STREAM", Http2FrameTypeToString(Http2FrameType::RST_STREAM));
  EXPECT_EQ("SETTINGS", Http2FrameTypeToString(Http2FrameType::SETTINGS));
  EXPECT_EQ("PUSH_PROMISE",
            Http2FrameTypeToString(Http2FrameType::PUSH_PROMISE));
  EXPECT_EQ("PING", Http2FrameTypeToString(Http2FrameType::PING));
  EXPECT_EQ("GOAWAY", Http2FrameTypeToString(Http2FrameType::GOAWAY));
  EXPECT_EQ("WINDOW_UPDATE",
            Http2FrameTypeToString(Http2FrameType::WINDOW_UPDATE));
  EXPECT_EQ("CONTINUATION",
            Http2FrameTypeToString(Http2FrameType::CONTINUATION));
  EXPECT_EQ("ALTSVC", Http2FrameTypeToString(Http2FrameType::ALTSVC));

  EXPECT_EQ("DATA", Http2FrameTypeToString(0));
  EXPECT_EQ("HEADERS", Http2FrameTypeToString(1));
  EXPECT_EQ("PRIORITY", Http2FrameTypeToString(2));
  EXPECT_EQ("RST_STREAM", Http2FrameTypeToString(3));
  EXPECT_EQ("SETTINGS", Http2FrameTypeToString(4));
  EXPECT_EQ("PUSH_PROMISE", Http2FrameTypeToString(5));
  EXPECT_EQ("PING", Http2FrameTypeToString(6));
  EXPECT_EQ("GOAWAY", Http2FrameTypeToString(7));
  EXPECT_EQ("WINDOW_UPDATE", Http2FrameTypeToString(8));
  EXPECT_EQ("CONTINUATION", Http2FrameTypeToString(9));
  EXPECT_EQ("ALTSVC", Http2FrameTypeToString(10));

  EXPECT_EQ("UnknownFrameType(99)", Http2FrameTypeToString(99));
}

TEST(Http2ConstantsTest, Http2FrameFlag) {
  EXPECT_EQ(Http2FrameFlag::END_STREAM, static_cast<Http2FrameFlag>(0x01));
  EXPECT_EQ(Http2FrameFlag::ACK, static_cast<Http2FrameFlag>(0x01));
  EXPECT_EQ(Http2FrameFlag::END_HEADERS, static_cast<Http2FrameFlag>(0x04));
  EXPECT_EQ(Http2FrameFlag::PADDED, static_cast<Http2FrameFlag>(0x08));
  EXPECT_EQ(Http2FrameFlag::PRIORITY, static_cast<Http2FrameFlag>(0x20));

  EXPECT_EQ(Http2FrameFlag::END_STREAM, 0x01);
  EXPECT_EQ(Http2FrameFlag::ACK, 0x01);
  EXPECT_EQ(Http2FrameFlag::END_HEADERS, 0x04);
  EXPECT_EQ(Http2FrameFlag::PADDED, 0x08);
  EXPECT_EQ(Http2FrameFlag::PRIORITY, 0x20);
}

TEST(Http2ConstantsTest, Http2FrameFlagsToString) {
  // Single flags...

  // 0b00000001
  EXPECT_EQ("END_STREAM", Http2FrameFlagsToString(Http2FrameType::DATA,
                                                  Http2FrameFlag::END_STREAM));
  EXPECT_EQ("END_STREAM",
            Http2FrameFlagsToString(Http2FrameType::HEADERS, 0x01));
  EXPECT_EQ("ACK", Http2FrameFlagsToString(Http2FrameType::SETTINGS,
                                           Http2FrameFlag::ACK));
  EXPECT_EQ("ACK", Http2FrameFlagsToString(Http2FrameType::PING, 0x01));

  // 0b00000010
  EXPECT_EQ("0x02", Http2FrameFlagsToString(0xff, 0x02));

  // 0b00000100
  EXPECT_EQ("END_HEADERS",
            Http2FrameFlagsToString(Http2FrameType::HEADERS,
                                    Http2FrameFlag::END_HEADERS));
  EXPECT_EQ("END_HEADERS",
            Http2FrameFlagsToString(Http2FrameType::PUSH_PROMISE, 0x04));
  EXPECT_EQ("END_HEADERS", Http2FrameFlagsToString(0x09, 0x04));
  EXPECT_EQ("0x04", Http2FrameFlagsToString(0xff, 0x04));

  // 0b00001000
  EXPECT_EQ("PADDED", Http2FrameFlagsToString(Http2FrameType::DATA,
                                              Http2FrameFlag::PADDED));
  EXPECT_EQ("PADDED", Http2FrameFlagsToString(Http2FrameType::HEADERS, 0x08));
  EXPECT_EQ("PADDED", Http2FrameFlagsToString(0x05, 0x08));
  EXPECT_EQ("0x08", Http2FrameFlagsToString(0xff, Http2FrameFlag::PADDED));

  // 0b00010000
  EXPECT_EQ("0x10", Http2FrameFlagsToString(Http2FrameType::SETTINGS, 0x10));

  // 0b00100000
  EXPECT_EQ("PRIORITY", Http2FrameFlagsToString(Http2FrameType::HEADERS, 0x20));
  EXPECT_EQ("0x20",
            Http2FrameFlagsToString(Http2FrameType::PUSH_PROMISE, 0x20));

  // 0b01000000
  EXPECT_EQ("0x40", Http2FrameFlagsToString(0xff, 0x40));

  // 0b10000000
  EXPECT_EQ("0x80", Http2FrameFlagsToString(0xff, 0x80));

  // Combined flags...

  EXPECT_EQ("END_STREAM|PADDED|0xf6",
            Http2FrameFlagsToString(Http2FrameType::DATA, 0xff));
  EXPECT_EQ("END_STREAM|END_HEADERS|PADDED|PRIORITY|0xd2",
            Http2FrameFlagsToString(Http2FrameType::HEADERS, 0xff));
  EXPECT_EQ("0xff", Http2FrameFlagsToString(Http2FrameType::PRIORITY, 0xff));
  EXPECT_EQ("0xff", Http2FrameFlagsToString(Http2FrameType::RST_STREAM, 0xff));
  EXPECT_EQ("ACK|0xfe",
            Http2FrameFlagsToString(Http2FrameType::SETTINGS, 0xff));
  EXPECT_EQ("END_HEADERS|PADDED|0xf3",
            Http2FrameFlagsToString(Http2FrameType::PUSH_PROMISE, 0xff));
  EXPECT_EQ("ACK|0xfe", Http2FrameFlagsToString(Http2FrameType::PING, 0xff));
  EXPECT_EQ("0xff", Http2FrameFlagsToString(Http2FrameType::GOAWAY, 0xff));
  EXPECT_EQ("0xff",
            Http2FrameFlagsToString(Http2FrameType::WINDOW_UPDATE, 0xff));
  EXPECT_EQ("END_HEADERS|0xfb",
            Http2FrameFlagsToString(Http2FrameType::CONTINUATION, 0xff));
  EXPECT_EQ("0xff", Http2FrameFlagsToString(Http2FrameType::ALTSVC, 0xff));
  EXPECT_EQ("0xff", Http2FrameFlagsToString(0xff, 0xff));
}

TEST(Http2ConstantsTest, Http2ErrorCode) {
  EXPECT_EQ(Http2ErrorCode::HTTP2_NO_ERROR, static_cast<Http2ErrorCode>(0x0));
  EXPECT_EQ(Http2ErrorCode::PROTOCOL_ERROR, static_cast<Http2ErrorCode>(0x1));
  EXPECT_EQ(Http2ErrorCode::INTERNAL_ERROR, static_cast<Http2ErrorCode>(0x2));
  EXPECT_EQ(Http2ErrorCode::FLOW_CONTROL_ERROR,
            static_cast<Http2ErrorCode>(0x3));
  EXPECT_EQ(Http2ErrorCode::SETTINGS_TIMEOUT, static_cast<Http2ErrorCode>(0x4));
  EXPECT_EQ(Http2ErrorCode::STREAM_CLOSED, static_cast<Http2ErrorCode>(0x5));
  EXPECT_EQ(Http2ErrorCode::FRAME_SIZE_ERROR, static_cast<Http2ErrorCode>(0x6));
  EXPECT_EQ(Http2ErrorCode::REFUSED_STREAM, static_cast<Http2ErrorCode>(0x7));
  EXPECT_EQ(Http2ErrorCode::CANCEL, static_cast<Http2ErrorCode>(0x8));
  EXPECT_EQ(Http2ErrorCode::COMPRESSION_ERROR,
            static_cast<Http2ErrorCode>(0x9));
  EXPECT_EQ(Http2ErrorCode::CONNECT_ERROR, static_cast<Http2ErrorCode>(0xa));
  EXPECT_EQ(Http2ErrorCode::ENHANCE_YOUR_CALM,
            static_cast<Http2ErrorCode>(0xb));
  EXPECT_EQ(Http2ErrorCode::INADEQUATE_SECURITY,
            static_cast<Http2ErrorCode>(0xc));
  EXPECT_EQ(Http2ErrorCode::HTTP_1_1_REQUIRED,
            static_cast<Http2ErrorCode>(0xd));
}

TEST(Http2ConstantsTest, Http2ErrorCodeToString) {
  EXPECT_EQ("NO_ERROR", Http2ErrorCodeToString(Http2ErrorCode::HTTP2_NO_ERROR));
  EXPECT_EQ("NO_ERROR", Http2ErrorCodeToString(0x0));
  EXPECT_EQ("PROTOCOL_ERROR",
            Http2ErrorCodeToString(Http2ErrorCode::PROTOCOL_ERROR));
  EXPECT_EQ("PROTOCOL_ERROR", Http2ErrorCodeToString(0x1));
  EXPECT_EQ("INTERNAL_ERROR",
            Http2ErrorCodeToString(Http2ErrorCode::INTERNAL_ERROR));
  EXPECT_EQ("INTERNAL_ERROR", Http2ErrorCodeToString(0x2));
  EXPECT_EQ("FLOW_CONTROL_ERROR",
            Http2ErrorCodeToString(Http2ErrorCode::FLOW_CONTROL_ERROR));
  EXPECT_EQ("FLOW_CONTROL_ERROR", Http2ErrorCodeToString(0x3));
  EXPECT_EQ("SETTINGS_TIMEOUT",
            Http2ErrorCodeToString(Http2ErrorCode::SETTINGS_TIMEOUT));
  EXPECT_EQ("SETTINGS_TIMEOUT", Http2ErrorCodeToString(0x4));
  EXPECT_EQ("STREAM_CLOSED",
            Http2ErrorCodeToString(Http2ErrorCode::STREAM_CLOSED));
  EXPECT_EQ("STREAM_CLOSED", Http2ErrorCodeToString(0x5));
  EXPECT_EQ("FRAME_SIZE_ERROR",
            Http2ErrorCodeToString(Http2ErrorCode::FRAME_SIZE_ERROR));
  EXPECT_EQ("FRAME_SIZE_ERROR", Http2ErrorCodeToString(0x6));
  EXPECT_EQ("REFUSED_STREAM",
            Http2ErrorCodeToString(Http2ErrorCode::REFUSED_STREAM));
  EXPECT_EQ("REFUSED_STREAM", Http2ErrorCodeToString(0x7));
  EXPECT_EQ("CANCEL", Http2ErrorCodeToString(Http2ErrorCode::CANCEL));
  EXPECT_EQ("CANCEL", Http2ErrorCodeToString(0x8));
  EXPECT_EQ("COMPRESSION_ERROR",
            Http2ErrorCodeToString(Http2ErrorCode::COMPRESSION_ERROR));
  EXPECT_EQ("COMPRESSION_ERROR", Http2ErrorCodeToString(0x9));
  EXPECT_EQ("CONNECT_ERROR",
            Http2ErrorCodeToString(Http2ErrorCode::CONNECT_ERROR));
  EXPECT_EQ("CONNECT_ERROR", Http2ErrorCodeToString(0xa));
  EXPECT_EQ("ENHANCE_YOUR_CALM",
            Http2ErrorCodeToString(Http2ErrorCode::ENHANCE_YOUR_CALM));
  EXPECT_EQ("ENHANCE_YOUR_CALM", Http2ErrorCodeToString(0xb));
  EXPECT_EQ("INADEQUATE_SECURITY",
            Http2ErrorCodeToString(Http2ErrorCode::INADEQUATE_SECURITY));
  EXPECT_EQ("INADEQUATE_SECURITY", Http2ErrorCodeToString(0xc));
  EXPECT_EQ("HTTP_1_1_REQUIRED",
            Http2ErrorCodeToString(Http2ErrorCode::HTTP_1_1_REQUIRED));
  EXPECT_EQ("HTTP_1_1_REQUIRED", Http2ErrorCodeToString(0xd));

  EXPECT_EQ("UnknownErrorCode(0x123)", Http2ErrorCodeToString(0x123));
}

TEST(Http2ConstantsTest, Http2SettingsParameter) {
  EXPECT_EQ(Http2SettingsParameter::HEADER_TABLE_SIZE,
            static_cast<Http2SettingsParameter>(0x1));
  EXPECT_EQ(Http2SettingsParameter::ENABLE_PUSH,
            static_cast<Http2SettingsParameter>(0x2));
  EXPECT_EQ(Http2SettingsParameter::MAX_CONCURRENT_STREAMS,
            static_cast<Http2SettingsParameter>(0x3));
  EXPECT_EQ(Http2SettingsParameter::INITIAL_WINDOW_SIZE,
            static_cast<Http2SettingsParameter>(0x4));
  EXPECT_EQ(Http2SettingsParameter::MAX_FRAME_SIZE,
            static_cast<Http2SettingsParameter>(0x5));
  EXPECT_EQ(Http2SettingsParameter::MAX_HEADER_LIST_SIZE,
            static_cast<Http2SettingsParameter>(0x6));

  EXPECT_TRUE(IsSupportedHttp2SettingsParameter(
      Http2SettingsParameter::HEADER_TABLE_SIZE));
  EXPECT_TRUE(
      IsSupportedHttp2SettingsParameter(Http2SettingsParameter::ENABLE_PUSH));
  EXPECT_TRUE(IsSupportedHttp2SettingsParameter(
      Http2SettingsParameter::MAX_CONCURRENT_STREAMS));
  EXPECT_TRUE(IsSupportedHttp2SettingsParameter(
      Http2SettingsParameter::INITIAL_WINDOW_SIZE));
  EXPECT_TRUE(IsSupportedHttp2SettingsParameter(
      Http2SettingsParameter::MAX_FRAME_SIZE));
  EXPECT_TRUE(IsSupportedHttp2SettingsParameter(
      Http2SettingsParameter::MAX_HEADER_LIST_SIZE));

  EXPECT_FALSE(IsSupportedHttp2SettingsParameter(
      static_cast<Http2SettingsParameter>(0)));
  EXPECT_FALSE(IsSupportedHttp2SettingsParameter(
      static_cast<Http2SettingsParameter>(7)));
}

TEST(Http2ConstantsTest, Http2SettingsParameterToString) {
  EXPECT_EQ("HEADER_TABLE_SIZE",
            Http2SettingsParameterToString(
                Http2SettingsParameter::HEADER_TABLE_SIZE));
  EXPECT_EQ("HEADER_TABLE_SIZE", Http2SettingsParameterToString(0x1));
  EXPECT_EQ("ENABLE_PUSH", Http2SettingsParameterToString(
                               Http2SettingsParameter::ENABLE_PUSH));
  EXPECT_EQ("ENABLE_PUSH", Http2SettingsParameterToString(0x2));
  EXPECT_EQ("MAX_CONCURRENT_STREAMS",
            Http2SettingsParameterToString(
                Http2SettingsParameter::MAX_CONCURRENT_STREAMS));
  EXPECT_EQ("MAX_CONCURRENT_STREAMS", Http2SettingsParameterToString(0x3));
  EXPECT_EQ("INITIAL_WINDOW_SIZE",
            Http2SettingsParameterToString(
                Http2SettingsParameter::INITIAL_WINDOW_SIZE));
  EXPECT_EQ("INITIAL_WINDOW_SIZE", Http2SettingsParameterToString(0x4));
  EXPECT_EQ("MAX_FRAME_SIZE", Http2SettingsParameterToString(
                                  Http2SettingsParameter::MAX_FRAME_SIZE));
  EXPECT_EQ("MAX_FRAME_SIZE", Http2SettingsParameterToString(0x5));
  EXPECT_EQ("MAX_HEADER_LIST_SIZE",
            Http2SettingsParameterToString(
                Http2SettingsParameter::MAX_HEADER_LIST_SIZE));
  EXPECT_EQ("MAX_HEADER_LIST_SIZE", Http2SettingsParameterToString(0x6));

  EXPECT_EQ("UnknownSettingsParameter(0x123)",
            Http2SettingsParameterToString(0x123));
}

}  // namespace
}  // namespace test
}  // namespace http2
