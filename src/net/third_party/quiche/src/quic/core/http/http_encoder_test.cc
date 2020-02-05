// Copyright (c) 2018 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/third_party/quiche/src/quic/core/http/http_encoder.h"

#include "net/third_party/quiche/src/quic/platform/api/quic_arraysize.h"
#include "net/third_party/quiche/src/quic/platform/api/quic_test.h"
#include "net/third_party/quiche/src/quic/test_tools/quic_test_utils.h"

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
  EXPECT_EQ(QUIC_ARRAYSIZE(output), length);
  CompareCharArraysWithHexError("DATA", buffer.get(), length, output,
                                QUIC_ARRAYSIZE(output));
}

TEST(HttpEncoderTest, SerializeHeadersFrameHeader) {
  std::unique_ptr<char[]> buffer;
  uint64_t length = HttpEncoder::SerializeHeadersFrameHeader(
      /* payload_length = */ 7, &buffer);
  char output[] = {// type (HEADERS)
                   0x01,
                   // length
                   0x07};
  EXPECT_EQ(QUIC_ARRAYSIZE(output), length);
  CompareCharArraysWithHexError("HEADERS", buffer.get(), length, output,
                                QUIC_ARRAYSIZE(output));
}

TEST(HttpEncoderTest, SerializePriorityFrame) {
  PriorityFrame priority;
  priority.prioritized_type = REQUEST_STREAM;
  priority.dependency_type = REQUEST_STREAM;
  priority.exclusive = true;
  priority.prioritized_element_id = 0x03;
  priority.element_dependency_id = 0x04;
  priority.weight = 0xFF;
  char output[] = {// type (PRIORITY)
                   0x2,
                   // length
                   0x4,
                   // request stream, request stream, exclusive
                   0x08,
                   // prioritized_element_id
                   0x03,
                   // element_dependency_id
                   0x04,
                   // weight
                   0xFF};

  std::unique_ptr<char[]> buffer;
  uint64_t length = HttpEncoder::SerializePriorityFrame(priority, &buffer);
  EXPECT_EQ(QUIC_ARRAYSIZE(output), length);
  CompareCharArraysWithHexError("PRIORITY", buffer.get(), length, output,
                                QUIC_ARRAYSIZE(output));

  PriorityFrame priority2;
  priority2.prioritized_type = ROOT_OF_TREE;
  priority2.dependency_type = REQUEST_STREAM;
  priority2.exclusive = true;
  priority2.element_dependency_id = 0x04;
  priority2.weight = 0xFF;
  char output2[] = {// type (PRIORIRTY)
                    0x2,
                    // length
                    0x3,
                    // root of tree, request stream, exclusive
                    0xc8,
                    // element_dependency_id
                    0x04,
                    // weight
                    0xff};
  length = HttpEncoder::SerializePriorityFrame(priority2, &buffer);
  EXPECT_EQ(QUIC_ARRAYSIZE(output2), length);
  CompareCharArraysWithHexError("PRIORITY", buffer.get(), length, output2,
                                QUIC_ARRAYSIZE(output2));

  PriorityFrame priority3;
  priority3.prioritized_type = ROOT_OF_TREE;
  priority3.dependency_type = ROOT_OF_TREE;
  priority3.exclusive = true;
  priority3.weight = 0xFF;
  char output3[] = {// type (PRIORITY)
                    0x2,
                    // length
                    0x2,
                    // root of tree, root of tree, exclusive
                    0xf8,
                    // weight
                    0xff};
  length = HttpEncoder::SerializePriorityFrame(priority3, &buffer);
  EXPECT_EQ(QUIC_ARRAYSIZE(output3), length);
  CompareCharArraysWithHexError("PRIORITY", buffer.get(), length, output3,
                                QUIC_ARRAYSIZE(output3));
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
  EXPECT_EQ(QUIC_ARRAYSIZE(output), length);
  CompareCharArraysWithHexError("CANCEL_PUSH", buffer.get(), length, output,
                                QUIC_ARRAYSIZE(output));
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
  EXPECT_EQ(QUIC_ARRAYSIZE(output), length);
  CompareCharArraysWithHexError("SETTINGS", buffer.get(), length, output,
                                QUIC_ARRAYSIZE(output));
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
  EXPECT_EQ(QUIC_ARRAYSIZE(output), length);
  CompareCharArraysWithHexError("PUSH_PROMISE", buffer.get(), length, output,
                                QUIC_ARRAYSIZE(output));
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
  EXPECT_EQ(QUIC_ARRAYSIZE(output), length);
  CompareCharArraysWithHexError("GOAWAY", buffer.get(), length, output,
                                QUIC_ARRAYSIZE(output));
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
  EXPECT_EQ(QUIC_ARRAYSIZE(output), length);
  CompareCharArraysWithHexError("MAX_PUSH_ID", buffer.get(), length, output,
                                QUIC_ARRAYSIZE(output));
}

TEST(HttpEncoderTest, SerializeDuplicatePushFrame) {
  DuplicatePushFrame duplicate_push;
  duplicate_push.push_id = 0x1;
  char output[] = {// type (DUPLICATE_PUSH)
                   0x0E,
                   // length
                   0x1,
                   // Push Id
                   0x01};
  std::unique_ptr<char[]> buffer;
  uint64_t length =
      HttpEncoder::SerializeDuplicatePushFrame(duplicate_push, &buffer);
  EXPECT_EQ(QUIC_ARRAYSIZE(output), length);
  CompareCharArraysWithHexError("DUPLICATE_PUSH", buffer.get(), length, output,
                                QUIC_ARRAYSIZE(output));
}

}  // namespace test
}  // namespace quic
