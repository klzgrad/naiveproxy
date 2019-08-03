// Copyright (c) 2018 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/third_party/quiche/src/quic/core/http/http_decoder.h"

#include "net/third_party/quiche/src/quic/core/http/http_encoder.h"
#include "net/third_party/quiche/src/quic/core/quic_data_writer.h"
#include "net/third_party/quiche/src/quic/platform/api/quic_arraysize.h"
#include "net/third_party/quiche/src/quic/platform/api/quic_ptr_util.h"
#include "net/third_party/quiche/src/quic/platform/api/quic_test.h"

using testing::InSequence;

namespace quic {

class MockVisitor : public HttpDecoder::Visitor {
 public:
  virtual ~MockVisitor() = default;

  // Called if an error is detected.
  MOCK_METHOD1(OnError, void(HttpDecoder* decoder));

  MOCK_METHOD1(OnPriorityFrame, void(const PriorityFrame& frame));
  MOCK_METHOD1(OnCancelPushFrame, void(const CancelPushFrame& frame));
  MOCK_METHOD1(OnMaxPushIdFrame, void(const MaxPushIdFrame& frame));
  MOCK_METHOD1(OnGoAwayFrame, void(const GoAwayFrame& frame));
  MOCK_METHOD1(OnSettingsFrameStart, void(Http3FrameLengths frame_lengths));
  MOCK_METHOD1(OnSettingsFrame, void(const SettingsFrame& frame));
  MOCK_METHOD1(OnDuplicatePushFrame, void(const DuplicatePushFrame& frame));

  MOCK_METHOD1(OnDataFrameStart, void(Http3FrameLengths frame_lengths));
  MOCK_METHOD1(OnDataFramePayload, void(QuicStringPiece payload));
  MOCK_METHOD0(OnDataFrameEnd, void());

  MOCK_METHOD1(OnHeadersFrameStart, void(Http3FrameLengths frame_lengths));
  MOCK_METHOD1(OnHeadersFramePayload, void(QuicStringPiece payload));
  MOCK_METHOD0(OnHeadersFrameEnd, void());

  MOCK_METHOD1(OnPushPromiseFrameStart, void(PushId push_id));
  MOCK_METHOD1(OnPushPromiseFramePayload, void(QuicStringPiece payload));
  MOCK_METHOD0(OnPushPromiseFrameEnd, void());
};

class HttpDecoderTest : public QuicTest {
 public:
  HttpDecoderTest() { decoder_.set_visitor(&visitor_); }
  HttpDecoder decoder_;
  testing::StrictMock<MockVisitor> visitor_;
};

TEST_F(HttpDecoderTest, InitialState) {
  EXPECT_EQ(QUIC_NO_ERROR, decoder_.error());
  EXPECT_EQ("", decoder_.error_detail());
}

TEST_F(HttpDecoderTest, ReservedFramesNoPayload) {
  std::unique_ptr<char[]> input;
  for (int n = 0; n < 8; ++n) {
    const uint8_t type = 0xB + 0x1F * n;
    QuicByteCount total_length = QuicDataWriter::GetVarInt62Len(0x00) +
                                 QuicDataWriter::GetVarInt62Len(type);
    input = QuicMakeUnique<char[]>(total_length);
    QuicDataWriter writer(total_length, input.get());
    writer.WriteVarInt62(type);
    writer.WriteVarInt62(0x00);

    EXPECT_EQ(total_length, decoder_.ProcessInput(input.get(), total_length))
        << n;
    EXPECT_EQ(QUIC_NO_ERROR, decoder_.error());
    ASSERT_EQ("", decoder_.error_detail());
    EXPECT_EQ(type, decoder_.current_frame_type());
  }
  // Test on a arbitrary reserved frame with 2-byte type field by hard coding
  // variable length integer.
  char in[] = {// type 0xB + 0x1F*3
               0x40, 0x68,
               // length
               0x00};
  EXPECT_EQ(3u, decoder_.ProcessInput(in, QUIC_ARRAYSIZE(in)));
  EXPECT_EQ(QUIC_NO_ERROR, decoder_.error());
  ASSERT_EQ("", decoder_.error_detail());
  EXPECT_EQ(0xB + 0x1F * 3u, decoder_.current_frame_type());
}

TEST_F(HttpDecoderTest, ReservedFramesSmallPayload) {
  std::unique_ptr<char[]> input;
  const uint8_t payload_size = 50;
  std::string data(payload_size, 'a');
  for (int n = 0; n < 8; ++n) {
    const uint8_t type = 0xB + 0x1F * n;
    QuicByteCount total_length = QuicDataWriter::GetVarInt62Len(payload_size) +
                                 QuicDataWriter::GetVarInt62Len(type) +
                                 payload_size;
    input = QuicMakeUnique<char[]>(total_length);
    QuicDataWriter writer(total_length, input.get());
    writer.WriteVarInt62(type);
    writer.WriteVarInt62(payload_size);
    writer.WriteStringPiece(data);
    EXPECT_EQ(total_length, decoder_.ProcessInput(input.get(), total_length))
        << n;
    EXPECT_EQ(QUIC_NO_ERROR, decoder_.error());
    ASSERT_EQ("", decoder_.error_detail());
    EXPECT_EQ(type, decoder_.current_frame_type());
  }

  // Test on a arbitrary reserved frame with 2-byte type field by hard coding
  // variable length integer.
  char in[payload_size + 3] = {// type 0xB + 0x1F*3
                               0x40, 0x68,
                               // length
                               payload_size};
  EXPECT_EQ(QUIC_ARRAYSIZE(in), decoder_.ProcessInput(in, QUIC_ARRAYSIZE(in)));
  EXPECT_EQ(QUIC_NO_ERROR, decoder_.error());
  ASSERT_EQ("", decoder_.error_detail());
  EXPECT_EQ(0xB + 0x1F * 3u, decoder_.current_frame_type());
}

TEST_F(HttpDecoderTest, ReservedFramesLargePayload) {
  std::unique_ptr<char[]> input;
  const QuicByteCount payload_size = 256;
  std::string data(payload_size, 'a');
  for (int n = 0; n < 8; ++n) {
    const uint8_t type = 0xB + 0x1F * n;
    QuicByteCount total_length = QuicDataWriter::GetVarInt62Len(payload_size) +
                                 QuicDataWriter::GetVarInt62Len(type) +
                                 payload_size;
    input = QuicMakeUnique<char[]>(total_length);
    QuicDataWriter writer(total_length, input.get());
    writer.WriteVarInt62(type);
    writer.WriteVarInt62(payload_size);
    writer.WriteStringPiece(data);

    EXPECT_EQ(total_length, decoder_.ProcessInput(input.get(), total_length))
        << n;
    EXPECT_EQ(QUIC_NO_ERROR, decoder_.error());
    ASSERT_EQ("", decoder_.error_detail());
    EXPECT_EQ(type, decoder_.current_frame_type());
  }

  // Test on a arbitrary reserved frame with 2-byte type field by hard coding
  // variable length integer.
  char in[payload_size + 4] = {// type 0xB + 0x1F*3
                               0x40, 0x68,
                               // length
                               0x40 + 0x01, 0x00};
  EXPECT_EQ(QUIC_ARRAYSIZE(in), decoder_.ProcessInput(in, QUIC_ARRAYSIZE(in)));
  EXPECT_EQ(QUIC_NO_ERROR, decoder_.error());
  ASSERT_EQ("", decoder_.error_detail());
  EXPECT_EQ(0xB + 0x1F * 3u, decoder_.current_frame_type());
}

TEST_F(HttpDecoderTest, CancelPush) {
  char input[] = {// type (CANCEL_PUSH)
                  0x03,
                  // length
                  0x1,
                  // Push Id
                  0x01};

  // Process the full frame.
  EXPECT_CALL(visitor_, OnCancelPushFrame(CancelPushFrame({1})));
  EXPECT_EQ(QUIC_ARRAYSIZE(input),
            decoder_.ProcessInput(input, QUIC_ARRAYSIZE(input)));
  EXPECT_EQ(QUIC_NO_ERROR, decoder_.error());
  EXPECT_EQ("", decoder_.error_detail());

  // Process the frame incremently.
  EXPECT_CALL(visitor_, OnCancelPushFrame(CancelPushFrame({1})));
  for (char c : input) {
    EXPECT_EQ(1u, decoder_.ProcessInput(&c, 1));
  }
  EXPECT_EQ(QUIC_NO_ERROR, decoder_.error());
  EXPECT_EQ("", decoder_.error_detail());
}

TEST_F(HttpDecoderTest, PushPromiseFrame) {
  char input[] = {// type (PUSH_PROMISE)
                  0x05,
                  // length
                  0x8,
                  // Push Id
                  0x01,
                  // Header Block
                  'H', 'e', 'a', 'd', 'e', 'r', 's'};

  // Process the full frame.
  InSequence s;
  EXPECT_CALL(visitor_, OnPushPromiseFrameStart(1));
  EXPECT_CALL(visitor_, OnPushPromiseFramePayload(QuicStringPiece("Headers")));
  EXPECT_CALL(visitor_, OnPushPromiseFrameEnd());
  EXPECT_EQ(QUIC_ARRAYSIZE(input),
            decoder_.ProcessInput(input, QUIC_ARRAYSIZE(input)));
  EXPECT_EQ(QUIC_NO_ERROR, decoder_.error());
  EXPECT_EQ("", decoder_.error_detail());

  // Process the frame incremently.
  EXPECT_CALL(visitor_, OnPushPromiseFrameStart(1));
  EXPECT_CALL(visitor_, OnPushPromiseFramePayload(QuicStringPiece("H")));
  EXPECT_CALL(visitor_, OnPushPromiseFramePayload(QuicStringPiece("e")));
  EXPECT_CALL(visitor_, OnPushPromiseFramePayload(QuicStringPiece("a")));
  EXPECT_CALL(visitor_, OnPushPromiseFramePayload(QuicStringPiece("d")));
  EXPECT_CALL(visitor_, OnPushPromiseFramePayload(QuicStringPiece("e")));
  EXPECT_CALL(visitor_, OnPushPromiseFramePayload(QuicStringPiece("r")));
  EXPECT_CALL(visitor_, OnPushPromiseFramePayload(QuicStringPiece("s")));
  EXPECT_CALL(visitor_, OnPushPromiseFrameEnd());
  for (char c : input) {
    EXPECT_EQ(1u, decoder_.ProcessInput(&c, 1));
  }
  EXPECT_EQ(QUIC_NO_ERROR, decoder_.error());
  EXPECT_EQ("", decoder_.error_detail());
}

TEST_F(HttpDecoderTest, MaxPushId) {
  char input[] = {// type (MAX_PUSH_ID)
                  0x0D,
                  // length
                  0x1,
                  // Push Id
                  0x01};

  // Process the full frame.
  EXPECT_CALL(visitor_, OnMaxPushIdFrame(MaxPushIdFrame({1})));
  EXPECT_EQ(QUIC_ARRAYSIZE(input),
            decoder_.ProcessInput(input, QUIC_ARRAYSIZE(input)));
  EXPECT_EQ(QUIC_NO_ERROR, decoder_.error());
  EXPECT_EQ("", decoder_.error_detail());

  // Process the frame incremently.
  EXPECT_CALL(visitor_, OnMaxPushIdFrame(MaxPushIdFrame({1})));
  for (char c : input) {
    EXPECT_EQ(1u, decoder_.ProcessInput(&c, 1));
  }
  EXPECT_EQ(QUIC_NO_ERROR, decoder_.error());
  EXPECT_EQ("", decoder_.error_detail());
}

TEST_F(HttpDecoderTest, DuplicatePush) {
  char input[] = {// type (DUPLICATE_PUSH)
                  0x0E,
                  // length
                  0x1,
                  // Push Id
                  0x01};
  // Process the full frame.
  EXPECT_CALL(visitor_, OnDuplicatePushFrame(DuplicatePushFrame({1})));
  EXPECT_EQ(QUIC_ARRAYSIZE(input),
            decoder_.ProcessInput(input, QUIC_ARRAYSIZE(input)));
  EXPECT_EQ(QUIC_NO_ERROR, decoder_.error());
  EXPECT_EQ("", decoder_.error_detail());

  // Process the frame incremently.
  EXPECT_CALL(visitor_, OnDuplicatePushFrame(DuplicatePushFrame({1})));
  for (char c : input) {
    EXPECT_EQ(1u, decoder_.ProcessInput(&c, 1));
  }
  EXPECT_EQ(QUIC_NO_ERROR, decoder_.error());
  EXPECT_EQ("", decoder_.error_detail());
}

TEST_F(HttpDecoderTest, PriorityFrame) {
  char input[] = {// type (PRIORITY)
                  0x2,
                  // length
                  0x4,
                  // request stream, request stream, exclusive
                  0x01,
                  // prioritized_element_id
                  0x03,
                  // element_dependency_id
                  0x04,
                  // weight
                  0xFF};

  PriorityFrame frame;
  frame.prioritized_type = REQUEST_STREAM;
  frame.dependency_type = REQUEST_STREAM;
  frame.exclusive = true;
  frame.prioritized_element_id = 0x03;
  frame.element_dependency_id = 0x04;
  frame.weight = 0xFF;

  // Process the full frame.
  EXPECT_CALL(visitor_, OnPriorityFrame(frame));
  EXPECT_EQ(QUIC_ARRAYSIZE(input),
            decoder_.ProcessInput(input, QUIC_ARRAYSIZE(input)));
  EXPECT_EQ(QUIC_NO_ERROR, decoder_.error());
  EXPECT_EQ("", decoder_.error_detail());

  /*
  // Process the frame incremently.
  EXPECT_CALL(visitor_, OnPriorityFrame(frame));
  for (char c : input) {
    EXPECT_EQ(1u, decoder_.ProcessInput(&c, 1));
  }
  EXPECT_EQ(QUIC_NO_ERROR, decoder_.error());
  EXPECT_EQ("", decoder_.error_detail());
  */
}

TEST_F(HttpDecoderTest, SettingsFrame) {
  // clang-format off
  char input[] = {
      // type (SETTINGS)
      0x04,
      // length
      0x07,
      // identifier (SETTINGS_NUM_PLACEHOLDERS)
      0x03,
      // content
      0x02,
      // identifier (SETTINGS_MAX_HEADER_LIST_SIZE)
      0x06,
      // content
      0x05,
      // identifier (256 in variable length integer)
      0x40 + 0x01,
      0x00,
      // content
      0x04};
  // clang-format on

  SettingsFrame frame;
  frame.values[3] = 2;
  frame.values[6] = 5;
  frame.values[256] = 4;

  // Process the full frame.
  EXPECT_CALL(visitor_, OnSettingsFrameStart(Http3FrameLengths(2, 7)));
  EXPECT_CALL(visitor_, OnSettingsFrame(frame));
  EXPECT_EQ(QUIC_ARRAYSIZE(input),
            decoder_.ProcessInput(input, QUIC_ARRAYSIZE(input)));
  EXPECT_EQ(QUIC_NO_ERROR, decoder_.error());
  EXPECT_EQ("", decoder_.error_detail());

  // Process the frame incremently.
  EXPECT_CALL(visitor_, OnSettingsFrameStart(Http3FrameLengths(2, 7)));
  EXPECT_CALL(visitor_, OnSettingsFrame(frame));
  for (char c : input) {
    EXPECT_EQ(1u, decoder_.ProcessInput(&c, 1));
  }
  EXPECT_EQ(QUIC_NO_ERROR, decoder_.error());
  EXPECT_EQ("", decoder_.error_detail());
}

TEST_F(HttpDecoderTest, DataFrame) {
  char input[] = {// type (DATA)
                  0x00,
                  // length
                  0x05,
                  // data
                  'D', 'a', 't', 'a', '!'};

  // Process the full frame.
  InSequence s;
  EXPECT_CALL(visitor_, OnDataFrameStart(Http3FrameLengths(2, 5)));
  EXPECT_CALL(visitor_, OnDataFramePayload(QuicStringPiece("Data!")));
  EXPECT_CALL(visitor_, OnDataFrameEnd());
  EXPECT_EQ(QUIC_ARRAYSIZE(input),
            decoder_.ProcessInput(input, QUIC_ARRAYSIZE(input)));
  EXPECT_EQ(QUIC_NO_ERROR, decoder_.error());
  EXPECT_EQ("", decoder_.error_detail());

  // Process the frame incremently.
  EXPECT_CALL(visitor_, OnDataFrameStart(Http3FrameLengths(2, 5)));
  EXPECT_CALL(visitor_, OnDataFramePayload(QuicStringPiece("D")));
  EXPECT_CALL(visitor_, OnDataFramePayload(QuicStringPiece("a")));
  EXPECT_CALL(visitor_, OnDataFramePayload(QuicStringPiece("t")));
  EXPECT_CALL(visitor_, OnDataFramePayload(QuicStringPiece("a")));
  EXPECT_CALL(visitor_, OnDataFramePayload(QuicStringPiece("!")));
  EXPECT_CALL(visitor_, OnDataFrameEnd());
  for (char c : input) {
    EXPECT_EQ(1u, decoder_.ProcessInput(&c, 1));
  }
  EXPECT_EQ(QUIC_NO_ERROR, decoder_.error());
  EXPECT_EQ("", decoder_.error_detail());
}

TEST_F(HttpDecoderTest, FrameHeaderPartialDelivery) {
  // A large input that will occupy more than 1 byte in the length field.
  std::string input(2048, 'x');
  HttpEncoder encoder;
  std::unique_ptr<char[]> buffer;
  QuicByteCount header_length =
      encoder.SerializeDataFrameHeader(input.length(), &buffer);
  std::string header = std::string(buffer.get(), header_length);
  // Partially send only 1 byte of the header to process.
  EXPECT_EQ(1u, decoder_.ProcessInput(header.data(), 1));
  EXPECT_EQ(QUIC_NO_ERROR, decoder_.error());
  EXPECT_EQ("", decoder_.error_detail());

  // Send the rest of the header.
  EXPECT_CALL(visitor_, OnDataFrameStart(Http3FrameLengths(3, 2048)));
  EXPECT_EQ(header_length - 1,
            decoder_.ProcessInput(header.data() + 1, header_length - 1));
  EXPECT_EQ(QUIC_NO_ERROR, decoder_.error());
  EXPECT_EQ("", decoder_.error_detail());

  // Send data.
  EXPECT_CALL(visitor_, OnDataFramePayload(QuicStringPiece(input)));
  EXPECT_CALL(visitor_, OnDataFrameEnd());
  EXPECT_EQ(2048u, decoder_.ProcessInput(input.data(), 2048));
  EXPECT_EQ(QUIC_NO_ERROR, decoder_.error());
  EXPECT_EQ("", decoder_.error_detail());
}

TEST_F(HttpDecoderTest, PartialDeliveryOfLargeFrameType) {
  // Use a reserved type that's more than 1 byte in VarInt62.
  const uint8_t type = 0xB + 0x1F * 3;
  std::unique_ptr<char[]> input;
  QuicByteCount total_length = QuicDataWriter::GetVarInt62Len(0x00) +
                               QuicDataWriter::GetVarInt62Len(type);
  input.reset(new char[total_length]);
  QuicDataWriter writer(total_length, input.get());
  writer.WriteVarInt62(type);
  writer.WriteVarInt62(0x00);

  auto raw_input = input.get();
  for (uint64_t i = 0; i < total_length; ++i) {
    char c = raw_input[i];
    EXPECT_EQ(1u, decoder_.ProcessInput(&c, 1));
  }
  EXPECT_EQ(QUIC_NO_ERROR, decoder_.error());
  EXPECT_EQ("", decoder_.error_detail());
  EXPECT_EQ(type, decoder_.current_frame_type());
}

TEST_F(HttpDecoderTest, GoAway) {
  char input[] = {// type (GOAWAY)
                  0x07,
                  // length
                  0x1,
                  // StreamId
                  0x01};

  // Process the full frame.
  EXPECT_CALL(visitor_, OnGoAwayFrame(GoAwayFrame({1})));
  EXPECT_EQ(QUIC_ARRAYSIZE(input),
            decoder_.ProcessInput(input, QUIC_ARRAYSIZE(input)));
  EXPECT_EQ(QUIC_NO_ERROR, decoder_.error());
  EXPECT_EQ("", decoder_.error_detail());

  // Process the frame incremently.
  EXPECT_CALL(visitor_, OnGoAwayFrame(GoAwayFrame({1})));
  for (char c : input) {
    EXPECT_EQ(1u, decoder_.ProcessInput(&c, 1));
  }
  EXPECT_EQ(QUIC_NO_ERROR, decoder_.error());
  EXPECT_EQ("", decoder_.error_detail());
}

TEST_F(HttpDecoderTest, HeadersFrame) {
  char input[] = {// type (HEADERS)
                  0x01,
                  // length
                  0x07,
                  // headers
                  'H', 'e', 'a', 'd', 'e', 'r', 's'};

  // Process the full frame.
  InSequence s;
  EXPECT_CALL(visitor_, OnHeadersFrameStart(Http3FrameLengths(2, 7)));
  EXPECT_CALL(visitor_, OnHeadersFramePayload(QuicStringPiece("Headers")));
  EXPECT_CALL(visitor_, OnHeadersFrameEnd());
  EXPECT_EQ(QUIC_ARRAYSIZE(input),
            decoder_.ProcessInput(input, QUIC_ARRAYSIZE(input)));
  EXPECT_EQ(QUIC_NO_ERROR, decoder_.error());
  EXPECT_EQ("", decoder_.error_detail());

  // Process the frame incremently.
  EXPECT_CALL(visitor_, OnHeadersFrameStart(Http3FrameLengths(2, 7)));
  EXPECT_CALL(visitor_, OnHeadersFramePayload(QuicStringPiece("H")));
  EXPECT_CALL(visitor_, OnHeadersFramePayload(QuicStringPiece("e")));
  EXPECT_CALL(visitor_, OnHeadersFramePayload(QuicStringPiece("a")));
  EXPECT_CALL(visitor_, OnHeadersFramePayload(QuicStringPiece("d")));
  EXPECT_CALL(visitor_, OnHeadersFramePayload(QuicStringPiece("e")));
  EXPECT_CALL(visitor_, OnHeadersFramePayload(QuicStringPiece("r")));
  EXPECT_CALL(visitor_, OnHeadersFramePayload(QuicStringPiece("s")));
  EXPECT_CALL(visitor_, OnHeadersFrameEnd());
  for (char c : input) {
    EXPECT_EQ(1u, decoder_.ProcessInput(&c, 1));
  }
  EXPECT_EQ(QUIC_NO_ERROR, decoder_.error());
  EXPECT_EQ("", decoder_.error_detail());
}

TEST_F(HttpDecoderTest, EmptyDataFrame) {
  char input[] = {0x00,   // type (DATA)
                  0x00};  // length

  // Process the full frame.
  InSequence s;
  EXPECT_CALL(visitor_, OnDataFrameStart(Http3FrameLengths(2, 0)));
  EXPECT_CALL(visitor_, OnDataFrameEnd());
  EXPECT_EQ(QUIC_ARRAYSIZE(input),
            decoder_.ProcessInput(input, QUIC_ARRAYSIZE(input)));
  EXPECT_EQ(QUIC_NO_ERROR, decoder_.error());
  EXPECT_EQ("", decoder_.error_detail());

  // Process the frame incremently.
  EXPECT_CALL(visitor_, OnDataFrameStart(Http3FrameLengths(2, 0)));
  EXPECT_CALL(visitor_, OnDataFrameEnd());
  for (char c : input) {
    EXPECT_EQ(1u, decoder_.ProcessInput(&c, 1));
  }
  EXPECT_EQ(QUIC_NO_ERROR, decoder_.error());
  EXPECT_EQ("", decoder_.error_detail());
}

TEST_F(HttpDecoderTest, EmptyHeadersFrame) {
  char input[] = {0x01,   // type (HEADERS)
                  0x00};  // length

  // Process the full frame.
  InSequence s;
  EXPECT_CALL(visitor_, OnHeadersFrameStart(Http3FrameLengths(2, 0)));
  EXPECT_CALL(visitor_, OnHeadersFrameEnd());
  EXPECT_EQ(QUIC_ARRAYSIZE(input),
            decoder_.ProcessInput(input, QUIC_ARRAYSIZE(input)));
  EXPECT_EQ(QUIC_NO_ERROR, decoder_.error());
  EXPECT_EQ("", decoder_.error_detail());

  // Process the frame incremently.
  EXPECT_CALL(visitor_, OnHeadersFrameStart(Http3FrameLengths(2, 0)));
  EXPECT_CALL(visitor_, OnHeadersFrameEnd());
  for (char c : input) {
    EXPECT_EQ(1u, decoder_.ProcessInput(&c, 1));
  }
  EXPECT_EQ(QUIC_NO_ERROR, decoder_.error());
  EXPECT_EQ("", decoder_.error_detail());
}

TEST_F(HttpDecoderTest, PushPromiseFrameNoHeaders) {
  char input[] = {0x05,   // type (PUSH_PROMISE)
                  0x01,   // length
                  0x01};  // Push Id

  // Process the full frame.
  InSequence s;
  EXPECT_CALL(visitor_, OnPushPromiseFrameStart(1));
  EXPECT_CALL(visitor_, OnPushPromiseFrameEnd());
  EXPECT_EQ(QUIC_ARRAYSIZE(input),
            decoder_.ProcessInput(input, QUIC_ARRAYSIZE(input)));
  EXPECT_EQ(QUIC_NO_ERROR, decoder_.error());
  EXPECT_EQ("", decoder_.error_detail());

  // Process the frame incremently.
  EXPECT_CALL(visitor_, OnPushPromiseFrameStart(1));
  EXPECT_CALL(visitor_, OnPushPromiseFrameEnd());
  for (char c : input) {
    EXPECT_EQ(1u, decoder_.ProcessInput(&c, 1));
  }
  EXPECT_EQ(QUIC_NO_ERROR, decoder_.error());
  EXPECT_EQ("", decoder_.error_detail());
}

TEST_F(HttpDecoderTest, MalformedFrameWithOverlyLargePayload) {
  char input[] = {0x03,   // type (CANCEL_PUSH)
                  0x10,   // length
                  0x15};  // malformed payload
  // Process the full frame.
  EXPECT_CALL(visitor_, OnError(&decoder_));
  EXPECT_EQ(0u, decoder_.ProcessInput(input, QUIC_ARRAYSIZE(input)));
  EXPECT_EQ(QUIC_INTERNAL_ERROR, decoder_.error());
  EXPECT_EQ("Frame is too large", decoder_.error_detail());
}

TEST_F(HttpDecoderTest, MalformedSettingsFrame) {
  char input[30];
  QuicDataWriter writer(30, input);
  // Write type SETTINGS.
  writer.WriteUInt8(0x04);
  // Write length.
  writer.WriteVarInt62(2048 * 1024);

  writer.WriteStringPiece("Malformed payload");
  EXPECT_CALL(visitor_, OnError(&decoder_));
  EXPECT_EQ(0u, decoder_.ProcessInput(input, QUIC_ARRAYSIZE(input)));
  EXPECT_EQ(QUIC_INTERNAL_ERROR, decoder_.error());
  EXPECT_EQ("Frame is too large", decoder_.error_detail());
}

}  // namespace quic
