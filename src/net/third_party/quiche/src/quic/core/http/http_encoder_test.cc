// Copyright (c) 2018 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/third_party/quiche/src/quic/core/http/http_encoder.h"

#include "net/third_party/quiche/src/quic/platform/api/quic_test.h"
#include "net/third_party/quiche/src/quic/test_tools/quic_test_utils.h"
#include "net/third_party/quiche/src/common/platform/api/quiche_arraysize.h"
#include "net/third_party/quiche/src/common/test_tools/quiche_test_utils.h"

namespace quic {
namespace test {

TEST(HttpEncoderTest, SerializeDataFrameHeader) {
  std::unique_ptr<char[]> buffer;
  uint64_t length =
      HttpEncoder::SerializeDataFrameHeader(/* payload_length = */ 5, &buffer);
  char output[] = {// type (DATA)
                   0x00,
                   // length
                   0x05};
  EXPECT_EQ(QUICHE_ARRAYSIZE(output), length);
  quiche::test::CompareCharArraysWithHexError("DATA", buffer.get(), length,
                                              output, QUICHE_ARRAYSIZE(output));
}

TEST(HttpEncoderTest, SerializeHeadersFrameHeader) {
  std::unique_ptr<char[]> buffer;
  uint64_t length = HttpEncoder::SerializeHeadersFrameHeader(
      /* payload_length = */ 7, &buffer);
  char output[] = {// type (HEADERS)
                   0x01,
                   // length
                   0x07};
  EXPECT_EQ(QUICHE_ARRAYSIZE(output), length);
  quiche::test::CompareCharArraysWithHexError("HEADERS", buffer.get(), length,
                                              output, QUICHE_ARRAYSIZE(output));
}

TEST(HttpEncoderTest, SerializeCancelPushFrame) {
  CancelPushFrame cancel_push;
  cancel_push.push_id = 0x01;
  char output[] = {// type (CANCEL_PUSH)
                   0x03,
                   // length
                   0x1,
                   // Push Id
                   0x01};
  std::unique_ptr<char[]> buffer;
  uint64_t length = HttpEncoder::SerializeCancelPushFrame(cancel_push, &buffer);
  EXPECT_EQ(QUICHE_ARRAYSIZE(output), length);
  quiche::test::CompareCharArraysWithHexError(
      "CANCEL_PUSH", buffer.get(), length, output, QUICHE_ARRAYSIZE(output));
}

TEST(HttpEncoderTest, SerializeSettingsFrame) {
  SettingsFrame settings;
  settings.values[1] = 2;
  settings.values[6] = 5;
  settings.values[256] = 4;
  char output[] = {// type (SETTINGS)
                   0x04,
                   // length
                   0x07,
                   // identifier (SETTINGS_QPACK_MAX_TABLE_CAPACITY)
                   0x01,
                   // content
                   0x02,
                   // identifier (SETTINGS_MAX_HEADER_LIST_SIZE)
                   0x06,
                   // content
                   0x05,
                   // identifier (256 in variable length integer)
                   0x40 + 0x01, 0x00,
                   // content
                   0x04};
  std::unique_ptr<char[]> buffer;
  uint64_t length = HttpEncoder::SerializeSettingsFrame(settings, &buffer);
  EXPECT_EQ(QUICHE_ARRAYSIZE(output), length);
  quiche::test::CompareCharArraysWithHexError("SETTINGS", buffer.get(), length,
                                              output, QUICHE_ARRAYSIZE(output));
}

TEST(HttpEncoderTest, SerializePushPromiseFrameWithOnlyPushId) {
  PushPromiseFrame push_promise;
  push_promise.push_id = 0x01;
  push_promise.headers = "Headers";
  char output[] = {// type (PUSH_PROMISE)
                   0x05,
                   // length
                   0x8,
                   // Push Id
                   0x01};
  std::unique_ptr<char[]> buffer;
  uint64_t length = HttpEncoder::SerializePushPromiseFrameWithOnlyPushId(
      push_promise, &buffer);
  EXPECT_EQ(QUICHE_ARRAYSIZE(output), length);
  quiche::test::CompareCharArraysWithHexError(
      "PUSH_PROMISE", buffer.get(), length, output, QUICHE_ARRAYSIZE(output));
}

TEST(HttpEncoderTest, SerializeGoAwayFrame) {
  GoAwayFrame goaway;
  goaway.stream_id = 0x1;
  char output[] = {// type (GOAWAY)
                   0x07,
                   // length
                   0x1,
                   // StreamId
                   0x01};
  std::unique_ptr<char[]> buffer;
  uint64_t length = HttpEncoder::SerializeGoAwayFrame(goaway, &buffer);
  EXPECT_EQ(QUICHE_ARRAYSIZE(output), length);
  quiche::test::CompareCharArraysWithHexError("GOAWAY", buffer.get(), length,
                                              output, QUICHE_ARRAYSIZE(output));
}

TEST(HttpEncoderTest, SerializeMaxPushIdFrame) {
  MaxPushIdFrame max_push_id;
  max_push_id.push_id = 0x1;
  char output[] = {// type (MAX_PUSH_ID)
                   0x0D,
                   // length
                   0x1,
                   // Push Id
                   0x01};
  std::unique_ptr<char[]> buffer;
  uint64_t length = HttpEncoder::SerializeMaxPushIdFrame(max_push_id, &buffer);
  EXPECT_EQ(QUICHE_ARRAYSIZE(output), length);
  quiche::test::CompareCharArraysWithHexError(
      "MAX_PUSH_ID", buffer.get(), length, output, QUICHE_ARRAYSIZE(output));
}

TEST(HttpEncoderTest, SerializePriorityUpdateFrame) {
  PriorityUpdateFrame priority_update1;
  priority_update1.prioritized_element_type = REQUEST_STREAM;
  priority_update1.prioritized_element_id = 0x03;
  char output1[] = {0x0f,   // type (PRIORITY_UPDATE)
                    0x02,   // length
                    0x00,   // prioritized element type: REQUEST_STREAM
                    0x03};  // prioritized element id

  std::unique_ptr<char[]> buffer;
  uint64_t length =
      HttpEncoder::SerializePriorityUpdateFrame(priority_update1, &buffer);
  EXPECT_EQ(QUICHE_ARRAYSIZE(output1), length);
  quiche::test::CompareCharArraysWithHexError("PRIORITY_UPDATE", buffer.get(),
                                              length, output1,
                                              QUICHE_ARRAYSIZE(output1));

  PriorityUpdateFrame priority_update2;
  priority_update2.prioritized_element_type = PUSH_STREAM;
  priority_update2.prioritized_element_id = 0x05;
  priority_update2.priority_field_value = "foo";
  char output2[] = {0x0f,             // type (PRIORIRTY)
                    0x05,             // length
                    0x80,             // prioritized element type: PUSH_STREAM
                    0x05,             // prioritized element id
                    'f',  'o', 'o'};  // priority field value
  length = HttpEncoder::SerializePriorityUpdateFrame(priority_update2, &buffer);
  EXPECT_EQ(QUICHE_ARRAYSIZE(output2), length);
  quiche::test::CompareCharArraysWithHexError("PRIORITY_UPDATE", buffer.get(),
                                              length, output2,
                                              QUICHE_ARRAYSIZE(output2));
}

}  // namespace test
}  // namespace quic
