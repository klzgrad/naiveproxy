// Copyright (c) 2018 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/third_party/quiche/src/quic/core/http/http_decoder.h"

#include <memory>
#include <utility>

#include "net/third_party/quiche/src/quic/core/http/http_encoder.h"
#include "net/third_party/quiche/src/quic/core/http/http_frames.h"
#include "net/third_party/quiche/src/quic/core/quic_data_writer.h"
#include "net/third_party/quiche/src/quic/core/quic_versions.h"
#include "net/third_party/quiche/src/quic/platform/api/quic_test.h"
#include "net/third_party/quiche/src/quic/test_tools/quic_test_utils.h"
#include "net/third_party/quiche/src/common/platform/api/quiche_arraysize.h"
#include "net/third_party/quiche/src/common/platform/api/quiche_str_cat.h"
#include "net/third_party/quiche/src/common/platform/api/quiche_string_piece.h"
#include "net/third_party/quiche/src/common/platform/api/quiche_text_utils.h"

using ::testing::_;
using ::testing::Eq;
using ::testing::InSequence;
using ::testing::Return;

namespace quic {

namespace test {

class HttpDecoderPeer {
 public:
  static uint64_t current_frame_type(HttpDecoder* decoder) {
    return decoder->current_frame_type_;
  }
};

class MockVisitor : public HttpDecoder::Visitor {
 public:
  ~MockVisitor() override = default;

  // Called if an error is detected.
  MOCK_METHOD1(OnError, void(HttpDecoder* decoder));

  MOCK_METHOD1(OnCancelPushFrame, bool(const CancelPushFrame& frame));
  MOCK_METHOD1(OnMaxPushIdFrame, bool(const MaxPushIdFrame& frame));
  MOCK_METHOD1(OnGoAwayFrame, bool(const GoAwayFrame& frame));
  MOCK_METHOD1(OnSettingsFrameStart, bool(QuicByteCount header_length));
  MOCK_METHOD1(OnSettingsFrame, bool(const SettingsFrame& frame));

  MOCK_METHOD2(OnDataFrameStart,
               bool(QuicByteCount header_length, QuicByteCount payload_length));
  MOCK_METHOD1(OnDataFramePayload, bool(quiche::QuicheStringPiece payload));
  MOCK_METHOD0(OnDataFrameEnd, bool());

  MOCK_METHOD2(OnHeadersFrameStart,
               bool(QuicByteCount header_length, QuicByteCount payload_length));
  MOCK_METHOD1(OnHeadersFramePayload, bool(quiche::QuicheStringPiece payload));
  MOCK_METHOD0(OnHeadersFrameEnd, bool());

  MOCK_METHOD1(OnPushPromiseFrameStart, bool(QuicByteCount header_length));
  MOCK_METHOD3(OnPushPromiseFramePushId,
               bool(PushId push_id,
                    QuicByteCount push_id_length,
                    QuicByteCount header_block_length));
  MOCK_METHOD1(OnPushPromiseFramePayload,
               bool(quiche::QuicheStringPiece payload));
  MOCK_METHOD0(OnPushPromiseFrameEnd, bool());

  MOCK_METHOD1(OnPriorityUpdateFrameStart, bool(QuicByteCount header_length));
  MOCK_METHOD1(OnPriorityUpdateFrame, bool(const PriorityUpdateFrame& frame));

  MOCK_METHOD3(OnUnknownFrameStart,
               bool(uint64_t frame_type,
                    QuicByteCount header_length,
                    QuicByteCount payload_length));
  MOCK_METHOD1(OnUnknownFramePayload, bool(quiche::QuicheStringPiece payload));
  MOCK_METHOD0(OnUnknownFrameEnd, bool());
};

class HttpDecoderTest : public QuicTest {
 public:
  HttpDecoderTest() : decoder_(&visitor_) {
    ON_CALL(visitor_, OnCancelPushFrame(_)).WillByDefault(Return(true));
    ON_CALL(visitor_, OnMaxPushIdFrame(_)).WillByDefault(Return(true));
    ON_CALL(visitor_, OnGoAwayFrame(_)).WillByDefault(Return(true));
    ON_CALL(visitor_, OnSettingsFrameStart(_)).WillByDefault(Return(true));
    ON_CALL(visitor_, OnSettingsFrame(_)).WillByDefault(Return(true));
    ON_CALL(visitor_, OnDataFrameStart(_, _)).WillByDefault(Return(true));
    ON_CALL(visitor_, OnDataFramePayload(_)).WillByDefault(Return(true));
    ON_CALL(visitor_, OnDataFrameEnd()).WillByDefault(Return(true));
    ON_CALL(visitor_, OnHeadersFrameStart(_, _)).WillByDefault(Return(true));
    ON_CALL(visitor_, OnHeadersFramePayload(_)).WillByDefault(Return(true));
    ON_CALL(visitor_, OnHeadersFrameEnd()).WillByDefault(Return(true));
    ON_CALL(visitor_, OnPushPromiseFrameStart(_)).WillByDefault(Return(true));
    ON_CALL(visitor_, OnPushPromiseFramePushId(_, _, _))
        .WillByDefault(Return(true));
    ON_CALL(visitor_, OnPushPromiseFramePayload(_)).WillByDefault(Return(true));
    ON_CALL(visitor_, OnPushPromiseFrameEnd()).WillByDefault(Return(true));
    ON_CALL(visitor_, OnPriorityUpdateFrameStart(_))
        .WillByDefault(Return(true));
    ON_CALL(visitor_, OnPriorityUpdateFrame(_)).WillByDefault(Return(true));
    ON_CALL(visitor_, OnUnknownFrameStart(_, _, _)).WillByDefault(Return(true));
    ON_CALL(visitor_, OnUnknownFramePayload(_)).WillByDefault(Return(true));
    ON_CALL(visitor_, OnUnknownFrameEnd()).WillByDefault(Return(true));
  }
  ~HttpDecoderTest() override = default;

  uint64_t current_frame_type() {
    return HttpDecoderPeer::current_frame_type(&decoder_);
  }

  // Process |input| in a single call to HttpDecoder::ProcessInput().
  QuicByteCount ProcessInput(quiche::QuicheStringPiece input) {
    return decoder_.ProcessInput(input.data(), input.size());
  }

  // Feed |input| to |decoder_| one character at a time,
  // verifying that each character gets processed.
  void ProcessInputCharByChar(quiche::QuicheStringPiece input) {
    for (char c : input) {
      EXPECT_EQ(1u, decoder_.ProcessInput(&c, 1));
    }
  }

  // Append garbage to |input|, then process it in a single call to
  // HttpDecoder::ProcessInput().  Verify that garbage is not read.
  QuicByteCount ProcessInputWithGarbageAppended(
      quiche::QuicheStringPiece input) {
    std::string input_with_garbage_appended =
        quiche::QuicheStrCat(input, "blahblah");
    QuicByteCount processed_bytes = ProcessInput(input_with_garbage_appended);

    // Guaranteed by HttpDecoder::ProcessInput() contract.
    DCHECK_LE(processed_bytes, input_with_garbage_appended.size());

    // Caller should set up visitor to pause decoding
    // before HttpDecoder would read garbage.
    EXPECT_LE(processed_bytes, input.size());

    return processed_bytes;
  }

  testing::StrictMock<MockVisitor> visitor_;
  HttpDecoder decoder_;
};

TEST_F(HttpDecoderTest, InitialState) {
  EXPECT_THAT(decoder_.error(), IsQuicNoError());
  EXPECT_EQ("", decoder_.error_detail());
}

TEST_F(HttpDecoderTest, UnknownFrame) {
  std::unique_ptr<char[]> input;

  const QuicByteCount payload_lengths[] = {0, 14, 100};
  const uint64_t frame_types[] = {
      0x21, 0x40, 0x5f, 0x7e, 0x9d,  // some reserved frame types
      0x06, 0x6f, 0x14               // some unknown, not reserved frame types
  };

  for (auto payload_length : payload_lengths) {
    std::string data(payload_length, 'a');

    for (auto frame_type : frame_types) {
      const QuicByteCount total_length =
          QuicDataWriter::GetVarInt62Len(frame_type) +
          QuicDataWriter::GetVarInt62Len(payload_length) + payload_length;
      input = std::make_unique<char[]>(total_length);

      QuicDataWriter writer(total_length, input.get());
      writer.WriteVarInt62(frame_type);
      writer.WriteVarInt62(payload_length);
      const QuicByteCount header_length = writer.length();
      if (payload_length > 0) {
        writer.WriteStringPiece(data);
      }

      EXPECT_CALL(visitor_, OnUnknownFrameStart(frame_type, header_length,
                                                payload_length));
      if (payload_length > 0) {
        EXPECT_CALL(visitor_, OnUnknownFramePayload(Eq(data)));
      }
      EXPECT_CALL(visitor_, OnUnknownFrameEnd());

      EXPECT_EQ(total_length, decoder_.ProcessInput(input.get(), total_length));

      EXPECT_THAT(decoder_.error(), IsQuicNoError());
      ASSERT_EQ("", decoder_.error_detail());
      EXPECT_EQ(frame_type, current_frame_type());
    }
  }
}

TEST_F(HttpDecoderTest, CancelPush) {
  InSequence s;
  std::string input = quiche::QuicheTextUtils::HexDecode(
      "03"    // type (CANCEL_PUSH)
      "01"    // length
      "01");  // Push Id

  // Visitor pauses processing.
  EXPECT_CALL(visitor_, OnCancelPushFrame(CancelPushFrame({1})))
      .WillOnce(Return(false));
  EXPECT_EQ(input.size(), ProcessInputWithGarbageAppended(input));
  EXPECT_THAT(decoder_.error(), IsQuicNoError());
  EXPECT_EQ("", decoder_.error_detail());

  // Process the full frame.
  EXPECT_CALL(visitor_, OnCancelPushFrame(CancelPushFrame({1})));
  EXPECT_EQ(input.size(), ProcessInput(input));
  EXPECT_THAT(decoder_.error(), IsQuicNoError());
  EXPECT_EQ("", decoder_.error_detail());

  // Process the frame incrementally.
  EXPECT_CALL(visitor_, OnCancelPushFrame(CancelPushFrame({1})));
  ProcessInputCharByChar(input);
  EXPECT_THAT(decoder_.error(), IsQuicNoError());
  EXPECT_EQ("", decoder_.error_detail());
}

TEST_F(HttpDecoderTest, PushPromiseFrame) {
  InSequence s;
  std::string input = quiche::QuicheStrCat(
      quiche::QuicheTextUtils::HexDecode("05"  // type (PUSH PROMISE)
                                         "0f"  // length
                                         "C000000000000101"),  // push id 257
      "Headers");                                              // headers

  // Visitor pauses processing.
  EXPECT_CALL(visitor_, OnPushPromiseFrameStart(2)).WillOnce(Return(false));
  EXPECT_CALL(visitor_, OnPushPromiseFramePushId(257, 8, 7))
      .WillOnce(Return(false));
  quiche::QuicheStringPiece remaining_input(input);
  QuicByteCount processed_bytes =
      ProcessInputWithGarbageAppended(remaining_input);
  EXPECT_EQ(2u, processed_bytes);
  remaining_input = remaining_input.substr(processed_bytes);
  processed_bytes = ProcessInputWithGarbageAppended(remaining_input);
  EXPECT_EQ(8u, processed_bytes);
  remaining_input = remaining_input.substr(processed_bytes);

  EXPECT_CALL(visitor_,
              OnPushPromiseFramePayload(quiche::QuicheStringPiece("Headers")))
      .WillOnce(Return(false));
  processed_bytes = ProcessInputWithGarbageAppended(remaining_input);
  EXPECT_EQ(remaining_input.size(), processed_bytes);

  EXPECT_CALL(visitor_, OnPushPromiseFrameEnd()).WillOnce(Return(false));
  EXPECT_EQ(0u, ProcessInputWithGarbageAppended(""));
  EXPECT_THAT(decoder_.error(), IsQuicNoError());
  EXPECT_EQ("", decoder_.error_detail());

  // Process the full frame.
  EXPECT_CALL(visitor_, OnPushPromiseFrameStart(2));
  EXPECT_CALL(visitor_, OnPushPromiseFramePushId(257, 8, 7));
  EXPECT_CALL(visitor_,
              OnPushPromiseFramePayload(quiche::QuicheStringPiece("Headers")));
  EXPECT_CALL(visitor_, OnPushPromiseFrameEnd());
  EXPECT_EQ(input.size(), ProcessInput(input));
  EXPECT_THAT(decoder_.error(), IsQuicNoError());
  EXPECT_EQ("", decoder_.error_detail());

  // Process the frame incrementally.
  EXPECT_CALL(visitor_, OnPushPromiseFrameStart(2));
  EXPECT_CALL(visitor_, OnPushPromiseFramePushId(257, 8, 7));
  EXPECT_CALL(visitor_,
              OnPushPromiseFramePayload(quiche::QuicheStringPiece("H")));
  EXPECT_CALL(visitor_,
              OnPushPromiseFramePayload(quiche::QuicheStringPiece("e")));
  EXPECT_CALL(visitor_,
              OnPushPromiseFramePayload(quiche::QuicheStringPiece("a")));
  EXPECT_CALL(visitor_,
              OnPushPromiseFramePayload(quiche::QuicheStringPiece("d")));
  EXPECT_CALL(visitor_,
              OnPushPromiseFramePayload(quiche::QuicheStringPiece("e")));
  EXPECT_CALL(visitor_,
              OnPushPromiseFramePayload(quiche::QuicheStringPiece("r")));
  EXPECT_CALL(visitor_,
              OnPushPromiseFramePayload(quiche::QuicheStringPiece("s")));
  EXPECT_CALL(visitor_, OnPushPromiseFrameEnd());
  ProcessInputCharByChar(input);
  EXPECT_THAT(decoder_.error(), IsQuicNoError());
  EXPECT_EQ("", decoder_.error_detail());

  // Process push id incrementally and append headers with last byte of push id.
  EXPECT_CALL(visitor_, OnPushPromiseFrameStart(2));
  EXPECT_CALL(visitor_, OnPushPromiseFramePushId(257, 8, 7));
  EXPECT_CALL(visitor_,
              OnPushPromiseFramePayload(quiche::QuicheStringPiece("Headers")));
  EXPECT_CALL(visitor_, OnPushPromiseFrameEnd());
  ProcessInputCharByChar(input.substr(0, 9));
  EXPECT_EQ(8u, ProcessInput(input.substr(9)));
  EXPECT_THAT(decoder_.error(), IsQuicNoError());
  EXPECT_EQ("", decoder_.error_detail());
}

TEST_F(HttpDecoderTest, CorruptPushPromiseFrame) {
  InSequence s;

  std::string input = quiche::QuicheTextUtils::HexDecode(
      "05"    // type (PUSH_PROMISE)
      "01"    // length
      "40");  // first byte of two-byte varint push id

  {
    HttpDecoder decoder(&visitor_);
    EXPECT_CALL(visitor_, OnPushPromiseFrameStart(2));
    EXPECT_CALL(visitor_, OnError(&decoder));

    decoder.ProcessInput(input.data(), input.size());

    EXPECT_THAT(decoder.error(), IsError(QUIC_HTTP_FRAME_ERROR));
    EXPECT_EQ("Unable to read PUSH_PROMISE push_id.", decoder.error_detail());
  }
  {
    HttpDecoder decoder(&visitor_);
    EXPECT_CALL(visitor_, OnPushPromiseFrameStart(2));
    EXPECT_CALL(visitor_, OnError(&decoder));

    for (auto c : input) {
      decoder.ProcessInput(&c, 1);
    }

    EXPECT_THAT(decoder.error(), IsError(QUIC_HTTP_FRAME_ERROR));
    EXPECT_EQ("Unable to read PUSH_PROMISE push_id.", decoder.error_detail());
  }
}

TEST_F(HttpDecoderTest, MaxPushId) {
  InSequence s;
  std::string input = quiche::QuicheTextUtils::HexDecode(
      "0D"    // type (MAX_PUSH_ID)
      "01"    // length
      "01");  // Push Id

  // Visitor pauses processing.
  EXPECT_CALL(visitor_, OnMaxPushIdFrame(MaxPushIdFrame({1})))
      .WillOnce(Return(false));
  EXPECT_EQ(input.size(), ProcessInputWithGarbageAppended(input));
  EXPECT_THAT(decoder_.error(), IsQuicNoError());
  EXPECT_EQ("", decoder_.error_detail());

  // Process the full frame.
  EXPECT_CALL(visitor_, OnMaxPushIdFrame(MaxPushIdFrame({1})));
  EXPECT_EQ(input.size(), ProcessInput(input));
  EXPECT_THAT(decoder_.error(), IsQuicNoError());
  EXPECT_EQ("", decoder_.error_detail());

  // Process the frame incrementally.
  EXPECT_CALL(visitor_, OnMaxPushIdFrame(MaxPushIdFrame({1})));
  ProcessInputCharByChar(input);
  EXPECT_THAT(decoder_.error(), IsQuicNoError());
  EXPECT_EQ("", decoder_.error_detail());
}

TEST_F(HttpDecoderTest, SettingsFrame) {
  InSequence s;
  std::string input = quiche::QuicheTextUtils::HexDecode(
      "04"    // type (SETTINGS)
      "07"    // length
      "01"    // identifier (SETTINGS_QPACK_MAX_TABLE_CAPACITY)
      "02"    // content
      "06"    // identifier (SETTINGS_MAX_HEADER_LIST_SIZE)
      "05"    // content
      "4100"  // identifier, encoded on 2 bytes (0x40), value is 256 (0x100)
      "04");  // content

  SettingsFrame frame;
  frame.values[1] = 2;
  frame.values[6] = 5;
  frame.values[256] = 4;

  // Visitor pauses processing.
  quiche::QuicheStringPiece remaining_input(input);
  EXPECT_CALL(visitor_, OnSettingsFrameStart(2)).WillOnce(Return(false));
  QuicByteCount processed_bytes =
      ProcessInputWithGarbageAppended(remaining_input);
  EXPECT_EQ(2u, processed_bytes);
  remaining_input = remaining_input.substr(processed_bytes);

  EXPECT_CALL(visitor_, OnSettingsFrame(frame)).WillOnce(Return(false));
  processed_bytes = ProcessInputWithGarbageAppended(remaining_input);
  EXPECT_EQ(remaining_input.size(), processed_bytes);
  EXPECT_THAT(decoder_.error(), IsQuicNoError());
  EXPECT_EQ("", decoder_.error_detail());

  // Process the full frame.
  EXPECT_CALL(visitor_, OnSettingsFrameStart(2));
  EXPECT_CALL(visitor_, OnSettingsFrame(frame));
  EXPECT_EQ(input.size(), ProcessInput(input));
  EXPECT_THAT(decoder_.error(), IsQuicNoError());
  EXPECT_EQ("", decoder_.error_detail());

  // Process the frame incrementally.
  EXPECT_CALL(visitor_, OnSettingsFrameStart(2));
  EXPECT_CALL(visitor_, OnSettingsFrame(frame));
  ProcessInputCharByChar(input);
  EXPECT_THAT(decoder_.error(), IsQuicNoError());
  EXPECT_EQ("", decoder_.error_detail());
}

TEST_F(HttpDecoderTest, CorruptSettingsFrame) {
  const char* const kPayload =
      "\x42\x11"                           // two-byte id
      "\x80\x22\x33\x44"                   // four-byte value
      "\x58\x39"                           // two-byte id
      "\xf0\x22\x33\x44\x55\x66\x77\x88";  // eight-byte value
  struct {
    size_t payload_length;
    const char* const error_message;
  } kTestData[] = {
      {1, "Unable to read setting identifier."},
      {5, "Unable to read setting value."},
      {7, "Unable to read setting identifier."},
      {12, "Unable to read setting value."},
  };

  for (const auto& test_data : kTestData) {
    std::string input;
    input.push_back(4u);  // type SETTINGS
    input.push_back(test_data.payload_length);
    const size_t header_length = input.size();
    input.append(kPayload, test_data.payload_length);

    HttpDecoder decoder(&visitor_);
    EXPECT_CALL(visitor_, OnSettingsFrameStart(header_length));
    EXPECT_CALL(visitor_, OnError(&decoder));

    QuicByteCount processed_bytes =
        decoder.ProcessInput(input.data(), input.size());
    EXPECT_EQ(input.size(), processed_bytes);
    EXPECT_THAT(decoder.error(), IsError(QUIC_HTTP_FRAME_ERROR));
    EXPECT_EQ(test_data.error_message, decoder.error_detail());
  }
}

TEST_F(HttpDecoderTest, DuplicateSettingsIdentifier) {
  std::string input = quiche::QuicheTextUtils::HexDecode(
      "04"    // type (SETTINGS)
      "04"    // length
      "01"    // identifier
      "01"    // content
      "01"    // identifier
      "02");  // content

  EXPECT_CALL(visitor_, OnSettingsFrameStart(2));
  EXPECT_CALL(visitor_, OnError(&decoder_));

  EXPECT_EQ(input.size(), ProcessInput(input));

  EXPECT_THAT(decoder_.error(),
              IsError(QUIC_HTTP_DUPLICATE_SETTING_IDENTIFIER));
  EXPECT_EQ("Duplicate setting identifier.", decoder_.error_detail());
}

TEST_F(HttpDecoderTest, DataFrame) {
  InSequence s;
  std::string input = quiche::QuicheStrCat(
      quiche::QuicheTextUtils::HexDecode("00"    // type (DATA)
                                         "05"),  // length
      "Data!");                                  // data

  // Visitor pauses processing.
  EXPECT_CALL(visitor_, OnDataFrameStart(2, 5)).WillOnce(Return(false));
  quiche::QuicheStringPiece remaining_input(input);
  QuicByteCount processed_bytes =
      ProcessInputWithGarbageAppended(remaining_input);
  EXPECT_EQ(2u, processed_bytes);
  remaining_input = remaining_input.substr(processed_bytes);

  EXPECT_CALL(visitor_, OnDataFramePayload(quiche::QuicheStringPiece("Data!")))
      .WillOnce(Return(false));
  processed_bytes = ProcessInputWithGarbageAppended(remaining_input);
  EXPECT_EQ(remaining_input.size(), processed_bytes);

  EXPECT_CALL(visitor_, OnDataFrameEnd()).WillOnce(Return(false));
  EXPECT_EQ(0u, ProcessInputWithGarbageAppended(""));
  EXPECT_THAT(decoder_.error(), IsQuicNoError());
  EXPECT_EQ("", decoder_.error_detail());

  // Process the full frame.
  EXPECT_CALL(visitor_, OnDataFrameStart(2, 5));
  EXPECT_CALL(visitor_, OnDataFramePayload(quiche::QuicheStringPiece("Data!")));
  EXPECT_CALL(visitor_, OnDataFrameEnd());
  EXPECT_EQ(input.size(), ProcessInput(input));
  EXPECT_THAT(decoder_.error(), IsQuicNoError());
  EXPECT_EQ("", decoder_.error_detail());

  // Process the frame incrementally.
  EXPECT_CALL(visitor_, OnDataFrameStart(2, 5));
  EXPECT_CALL(visitor_, OnDataFramePayload(quiche::QuicheStringPiece("D")));
  EXPECT_CALL(visitor_, OnDataFramePayload(quiche::QuicheStringPiece("a")));
  EXPECT_CALL(visitor_, OnDataFramePayload(quiche::QuicheStringPiece("t")));
  EXPECT_CALL(visitor_, OnDataFramePayload(quiche::QuicheStringPiece("a")));
  EXPECT_CALL(visitor_, OnDataFramePayload(quiche::QuicheStringPiece("!")));
  EXPECT_CALL(visitor_, OnDataFrameEnd());
  ProcessInputCharByChar(input);
  EXPECT_THAT(decoder_.error(), IsQuicNoError());
  EXPECT_EQ("", decoder_.error_detail());
}

TEST_F(HttpDecoderTest, FrameHeaderPartialDelivery) {
  InSequence s;
  // A large input that will occupy more than 1 byte in the length field.
  std::string input(2048, 'x');
  std::unique_ptr<char[]> buffer;
  QuicByteCount header_length =
      HttpEncoder::SerializeDataFrameHeader(input.length(), &buffer);
  std::string header = std::string(buffer.get(), header_length);
  // Partially send only 1 byte of the header to process.
  EXPECT_EQ(1u, decoder_.ProcessInput(header.data(), 1));
  EXPECT_THAT(decoder_.error(), IsQuicNoError());
  EXPECT_EQ("", decoder_.error_detail());

  // Send the rest of the header.
  EXPECT_CALL(visitor_, OnDataFrameStart(3, input.length()));
  EXPECT_EQ(header_length - 1,
            decoder_.ProcessInput(header.data() + 1, header_length - 1));
  EXPECT_THAT(decoder_.error(), IsQuicNoError());
  EXPECT_EQ("", decoder_.error_detail());

  // Send data.
  EXPECT_CALL(visitor_, OnDataFramePayload(quiche::QuicheStringPiece(input)));
  EXPECT_CALL(visitor_, OnDataFrameEnd());
  EXPECT_EQ(2048u, decoder_.ProcessInput(input.data(), 2048));
  EXPECT_THAT(decoder_.error(), IsQuicNoError());
  EXPECT_EQ("", decoder_.error_detail());
}

TEST_F(HttpDecoderTest, PartialDeliveryOfLargeFrameType) {
  // Use a reserved type that takes four bytes as a varint.
  const uint64_t frame_type = 0x1f * 0x222 + 0x21;
  const QuicByteCount payload_length = 0;
  const QuicByteCount header_length =
      QuicDataWriter::GetVarInt62Len(frame_type) +
      QuicDataWriter::GetVarInt62Len(payload_length);

  auto input = std::make_unique<char[]>(header_length);
  QuicDataWriter writer(header_length, input.get());
  writer.WriteVarInt62(frame_type);
  writer.WriteVarInt62(payload_length);

  EXPECT_CALL(visitor_,
              OnUnknownFrameStart(frame_type, header_length, payload_length));
  EXPECT_CALL(visitor_, OnUnknownFrameEnd());

  auto raw_input = input.get();
  for (uint64_t i = 0; i < header_length; ++i) {
    char c = raw_input[i];
    EXPECT_EQ(1u, decoder_.ProcessInput(&c, 1));
  }

  EXPECT_THAT(decoder_.error(), IsQuicNoError());
  EXPECT_EQ("", decoder_.error_detail());
  EXPECT_EQ(frame_type, current_frame_type());
}

TEST_F(HttpDecoderTest, GoAway) {
  InSequence s;
  std::string input = quiche::QuicheTextUtils::HexDecode(
      "07"    // type (GOAWAY)
      "01"    // length
      "01");  // StreamId

  // Visitor pauses processing.
  EXPECT_CALL(visitor_, OnGoAwayFrame(GoAwayFrame({1})))
      .WillOnce(Return(false));
  EXPECT_EQ(input.size(), ProcessInputWithGarbageAppended(input));
  EXPECT_THAT(decoder_.error(), IsQuicNoError());
  EXPECT_EQ("", decoder_.error_detail());

  // Process the full frame.
  EXPECT_CALL(visitor_, OnGoAwayFrame(GoAwayFrame({1})));
  EXPECT_EQ(input.size(), ProcessInput(input));
  EXPECT_THAT(decoder_.error(), IsQuicNoError());
  EXPECT_EQ("", decoder_.error_detail());

  // Process the frame incrementally.
  EXPECT_CALL(visitor_, OnGoAwayFrame(GoAwayFrame({1})));
  ProcessInputCharByChar(input);
  EXPECT_THAT(decoder_.error(), IsQuicNoError());
  EXPECT_EQ("", decoder_.error_detail());
}

TEST_F(HttpDecoderTest, HeadersFrame) {
  InSequence s;
  std::string input = quiche::QuicheStrCat(
      quiche::QuicheTextUtils::HexDecode("01"    // type (HEADERS)
                                         "07"),  // length
      "Headers");                                // headers

  // Visitor pauses processing.
  EXPECT_CALL(visitor_, OnHeadersFrameStart(2, 7)).WillOnce(Return(false));
  quiche::QuicheStringPiece remaining_input(input);
  QuicByteCount processed_bytes =
      ProcessInputWithGarbageAppended(remaining_input);
  EXPECT_EQ(2u, processed_bytes);
  remaining_input = remaining_input.substr(processed_bytes);

  EXPECT_CALL(visitor_,
              OnHeadersFramePayload(quiche::QuicheStringPiece("Headers")))
      .WillOnce(Return(false));
  processed_bytes = ProcessInputWithGarbageAppended(remaining_input);
  EXPECT_EQ(remaining_input.size(), processed_bytes);

  EXPECT_CALL(visitor_, OnHeadersFrameEnd()).WillOnce(Return(false));
  EXPECT_EQ(0u, ProcessInputWithGarbageAppended(""));
  EXPECT_THAT(decoder_.error(), IsQuicNoError());
  EXPECT_EQ("", decoder_.error_detail());

  // Process the full frame.
  EXPECT_CALL(visitor_, OnHeadersFrameStart(2, 7));
  EXPECT_CALL(visitor_,
              OnHeadersFramePayload(quiche::QuicheStringPiece("Headers")));
  EXPECT_CALL(visitor_, OnHeadersFrameEnd());
  EXPECT_EQ(input.size(), ProcessInput(input));
  EXPECT_THAT(decoder_.error(), IsQuicNoError());
  EXPECT_EQ("", decoder_.error_detail());

  // Process the frame incrementally.
  EXPECT_CALL(visitor_, OnHeadersFrameStart(2, 7));
  EXPECT_CALL(visitor_, OnHeadersFramePayload(quiche::QuicheStringPiece("H")));
  EXPECT_CALL(visitor_, OnHeadersFramePayload(quiche::QuicheStringPiece("e")));
  EXPECT_CALL(visitor_, OnHeadersFramePayload(quiche::QuicheStringPiece("a")));
  EXPECT_CALL(visitor_, OnHeadersFramePayload(quiche::QuicheStringPiece("d")));
  EXPECT_CALL(visitor_, OnHeadersFramePayload(quiche::QuicheStringPiece("e")));
  EXPECT_CALL(visitor_, OnHeadersFramePayload(quiche::QuicheStringPiece("r")));
  EXPECT_CALL(visitor_, OnHeadersFramePayload(quiche::QuicheStringPiece("s")));
  EXPECT_CALL(visitor_, OnHeadersFrameEnd());
  ProcessInputCharByChar(input);
  EXPECT_THAT(decoder_.error(), IsQuicNoError());
  EXPECT_EQ("", decoder_.error_detail());
}

TEST_F(HttpDecoderTest, EmptyDataFrame) {
  InSequence s;
  std::string input = quiche::QuicheTextUtils::HexDecode(
      "00"    // type (DATA)
      "00");  // length

  // Visitor pauses processing.
  EXPECT_CALL(visitor_, OnDataFrameStart(2, 0)).WillOnce(Return(false));
  EXPECT_EQ(input.size(), ProcessInputWithGarbageAppended(input));

  EXPECT_CALL(visitor_, OnDataFrameEnd()).WillOnce(Return(false));
  EXPECT_EQ(0u, ProcessInputWithGarbageAppended(""));
  EXPECT_THAT(decoder_.error(), IsQuicNoError());
  EXPECT_EQ("", decoder_.error_detail());

  // Process the full frame.
  EXPECT_CALL(visitor_, OnDataFrameStart(2, 0));
  EXPECT_CALL(visitor_, OnDataFrameEnd());
  EXPECT_EQ(input.size(), ProcessInput(input));
  EXPECT_THAT(decoder_.error(), IsQuicNoError());
  EXPECT_EQ("", decoder_.error_detail());

  // Process the frame incrementally.
  EXPECT_CALL(visitor_, OnDataFrameStart(2, 0));
  EXPECT_CALL(visitor_, OnDataFrameEnd());
  ProcessInputCharByChar(input);
  EXPECT_THAT(decoder_.error(), IsQuicNoError());
  EXPECT_EQ("", decoder_.error_detail());
}

TEST_F(HttpDecoderTest, EmptyHeadersFrame) {
  InSequence s;
  std::string input = quiche::QuicheTextUtils::HexDecode(
      "01"    // type (HEADERS)
      "00");  // length

  // Visitor pauses processing.
  EXPECT_CALL(visitor_, OnHeadersFrameStart(2, 0)).WillOnce(Return(false));
  EXPECT_EQ(input.size(), ProcessInputWithGarbageAppended(input));

  EXPECT_CALL(visitor_, OnHeadersFrameEnd()).WillOnce(Return(false));
  EXPECT_EQ(0u, ProcessInputWithGarbageAppended(""));
  EXPECT_THAT(decoder_.error(), IsQuicNoError());
  EXPECT_EQ("", decoder_.error_detail());

  // Process the full frame.
  EXPECT_CALL(visitor_, OnHeadersFrameStart(2, 0));
  EXPECT_CALL(visitor_, OnHeadersFrameEnd());
  EXPECT_EQ(input.size(), ProcessInput(input));
  EXPECT_THAT(decoder_.error(), IsQuicNoError());
  EXPECT_EQ("", decoder_.error_detail());

  // Process the frame incrementally.
  EXPECT_CALL(visitor_, OnHeadersFrameStart(2, 0));
  EXPECT_CALL(visitor_, OnHeadersFrameEnd());
  ProcessInputCharByChar(input);
  EXPECT_THAT(decoder_.error(), IsQuicNoError());
  EXPECT_EQ("", decoder_.error_detail());
}

TEST_F(HttpDecoderTest, PushPromiseFrameNoHeaders) {
  InSequence s;
  std::string input = quiche::QuicheTextUtils::HexDecode(
      "05"    // type (PUSH_PROMISE)
      "01"    // length
      "01");  // Push Id

  // Visitor pauses processing.
  EXPECT_CALL(visitor_, OnPushPromiseFrameStart(2));
  EXPECT_CALL(visitor_, OnPushPromiseFramePushId(1, 1, 0))
      .WillOnce(Return(false));
  EXPECT_EQ(input.size(), ProcessInputWithGarbageAppended(input));

  EXPECT_CALL(visitor_, OnPushPromiseFrameEnd()).WillOnce(Return(false));
  EXPECT_EQ(0u, ProcessInputWithGarbageAppended(""));
  EXPECT_THAT(decoder_.error(), IsQuicNoError());
  EXPECT_EQ("", decoder_.error_detail());

  // Process the full frame.
  EXPECT_CALL(visitor_, OnPushPromiseFrameStart(2));
  EXPECT_CALL(visitor_, OnPushPromiseFramePushId(1, 1, 0));
  EXPECT_CALL(visitor_, OnPushPromiseFrameEnd());
  EXPECT_EQ(input.size(), ProcessInput(input));
  EXPECT_THAT(decoder_.error(), IsQuicNoError());
  EXPECT_EQ("", decoder_.error_detail());

  // Process the frame incrementally.
  EXPECT_CALL(visitor_, OnPushPromiseFrameStart(2));
  EXPECT_CALL(visitor_, OnPushPromiseFramePushId(1, 1, 0));
  EXPECT_CALL(visitor_, OnPushPromiseFrameEnd());
  ProcessInputCharByChar(input);
  EXPECT_THAT(decoder_.error(), IsQuicNoError());
  EXPECT_EQ("", decoder_.error_detail());
}

TEST_F(HttpDecoderTest, MalformedFrameWithOverlyLargePayload) {
  std::string input = quiche::QuicheTextUtils::HexDecode(
      "03"    // type (CANCEL_PUSH)
      "10"    // length
      "15");  // malformed payload
  // Process the full frame.
  EXPECT_CALL(visitor_, OnError(&decoder_));
  EXPECT_EQ(2u, ProcessInput(input));
  EXPECT_THAT(decoder_.error(), IsError(QUIC_HTTP_FRAME_TOO_LARGE));
  EXPECT_EQ("Frame is too large.", decoder_.error_detail());
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
  EXPECT_EQ(5u, decoder_.ProcessInput(input, QUICHE_ARRAYSIZE(input)));
  EXPECT_THAT(decoder_.error(), IsError(QUIC_HTTP_FRAME_TOO_LARGE));
  EXPECT_EQ("Frame is too large.", decoder_.error_detail());
}

TEST_F(HttpDecoderTest, HeadersPausedThenData) {
  InSequence s;
  std::string input = quiche::QuicheStrCat(
      quiche::QuicheTextUtils::HexDecode("01"    // type (HEADERS)
                                         "07"),  // length
      "Headers",                                 // headers
      quiche::QuicheTextUtils::HexDecode("00"    // type (DATA)
                                         "05"),  // length
      "Data!");                                  // data

  // Visitor pauses processing, maybe because header decompression is blocked.
  EXPECT_CALL(visitor_, OnHeadersFrameStart(2, 7));
  EXPECT_CALL(visitor_,
              OnHeadersFramePayload(quiche::QuicheStringPiece("Headers")));
  EXPECT_CALL(visitor_, OnHeadersFrameEnd()).WillOnce(Return(false));
  quiche::QuicheStringPiece remaining_input(input);
  QuicByteCount processed_bytes =
      ProcessInputWithGarbageAppended(remaining_input);
  EXPECT_EQ(9u, processed_bytes);
  remaining_input = remaining_input.substr(processed_bytes);

  // Process DATA frame.
  EXPECT_CALL(visitor_, OnDataFrameStart(2, 5));
  EXPECT_CALL(visitor_, OnDataFramePayload(quiche::QuicheStringPiece("Data!")));
  EXPECT_CALL(visitor_, OnDataFrameEnd());

  processed_bytes = ProcessInput(remaining_input);
  EXPECT_EQ(remaining_input.size(), processed_bytes);

  EXPECT_THAT(decoder_.error(), IsQuicNoError());
  EXPECT_EQ("", decoder_.error_detail());
}

TEST_F(HttpDecoderTest, CorruptFrame) {
  InSequence s;

  struct {
    const char* const input;
    const char* const error_message;
  } kTestData[] = {{"\x03"   // type (CANCEL_PUSH)
                    "\x01"   // length
                    "\x40",  // first byte of two-byte varint push id
                    "Unable to read CANCEL_PUSH push_id."},
                   {"\x03"  // type (CANCEL_PUSH)
                    "\x04"  // length
                    "\x05"  // valid push id
                    "foo",  // superfluous data
                    "Superfluous data in CANCEL_PUSH frame."},
                   {"\x0D"   // type (MAX_PUSH_ID)
                    "\x01"   // length
                    "\x40",  // first byte of two-byte varint push id
                    "Unable to read MAX_PUSH_ID push_id."},
                   {"\x0D"  // type (MAX_PUSH_ID)
                    "\x04"  // length
                    "\x05"  // valid push id
                    "foo",  // superfluous data
                    "Superfluous data in MAX_PUSH_ID frame."},
                   {"\x07"   // type (GOAWAY)
                    "\x01"   // length
                    "\x40",  // first byte of two-byte varint stream id
                    "Unable to read GOAWAY stream_id."},
                   {"\x07"  // type (GOAWAY)
                    "\x04"  // length
                    "\x05"  // valid stream id
                    "foo",  // superfluous data
                    "Superfluous data in GOAWAY frame."}};

  for (const auto& test_data : kTestData) {
    {
      HttpDecoder decoder(&visitor_);
      EXPECT_CALL(visitor_, OnError(&decoder));

      quiche::QuicheStringPiece input(test_data.input);
      decoder.ProcessInput(input.data(), input.size());
      EXPECT_THAT(decoder.error(), IsError(QUIC_HTTP_FRAME_ERROR));
      EXPECT_EQ(test_data.error_message, decoder.error_detail());
    }
    {
      HttpDecoder decoder(&visitor_);
      EXPECT_CALL(visitor_, OnError(&decoder));

      quiche::QuicheStringPiece input(test_data.input);
      for (auto c : input) {
        decoder.ProcessInput(&c, 1);
      }
      EXPECT_THAT(decoder.error(), IsError(QUIC_HTTP_FRAME_ERROR));
      EXPECT_EQ(test_data.error_message, decoder.error_detail());
    }
  }
}

TEST_F(HttpDecoderTest, EmptyCancelPushFrame) {
  std::string input = quiche::QuicheTextUtils::HexDecode(
      "03"    // type (CANCEL_PUSH)
      "00");  // frame length

  EXPECT_CALL(visitor_, OnError(&decoder_));
  EXPECT_EQ(input.size(), ProcessInput(input));
  EXPECT_THAT(decoder_.error(), IsError(QUIC_HTTP_FRAME_ERROR));
  EXPECT_EQ("Unable to read CANCEL_PUSH push_id.", decoder_.error_detail());
}

TEST_F(HttpDecoderTest, EmptySettingsFrame) {
  std::string input = quiche::QuicheTextUtils::HexDecode(
      "04"    // type (SETTINGS)
      "00");  // frame length

  EXPECT_CALL(visitor_, OnSettingsFrameStart(2));

  SettingsFrame empty_frame;
  EXPECT_CALL(visitor_, OnSettingsFrame(empty_frame));

  EXPECT_EQ(input.size(), ProcessInput(input));
  EXPECT_THAT(decoder_.error(), IsQuicNoError());
  EXPECT_EQ("", decoder_.error_detail());
}

// Regression test for https://crbug.com/1001823.
TEST_F(HttpDecoderTest, EmptyPushPromiseFrame) {
  std::string input = quiche::QuicheTextUtils::HexDecode(
      "05"    // type (PUSH_PROMISE)
      "00");  // frame length

  EXPECT_CALL(visitor_, OnError(&decoder_));
  EXPECT_EQ(input.size(), ProcessInput(input));
  EXPECT_THAT(decoder_.error(), IsError(QUIC_HTTP_FRAME_ERROR));
  EXPECT_EQ("PUSH_PROMISE frame with empty payload.", decoder_.error_detail());
}

TEST_F(HttpDecoderTest, EmptyGoAwayFrame) {
  std::string input = quiche::QuicheTextUtils::HexDecode(
      "07"    // type (GOAWAY)
      "00");  // frame length

  EXPECT_CALL(visitor_, OnError(&decoder_));
  EXPECT_EQ(input.size(), ProcessInput(input));
  EXPECT_THAT(decoder_.error(), IsError(QUIC_HTTP_FRAME_ERROR));
  EXPECT_EQ("Unable to read GOAWAY stream_id.", decoder_.error_detail());
}

TEST_F(HttpDecoderTest, EmptyMaxPushIdFrame) {
  std::string input = quiche::QuicheTextUtils::HexDecode(
      "0d"    // type (MAX_PUSH_ID)
      "00");  // frame length

  EXPECT_CALL(visitor_, OnError(&decoder_));
  EXPECT_EQ(input.size(), ProcessInput(input));
  EXPECT_THAT(decoder_.error(), IsError(QUIC_HTTP_FRAME_ERROR));
  EXPECT_EQ("Unable to read MAX_PUSH_ID push_id.", decoder_.error_detail());
}

TEST_F(HttpDecoderTest, LargeStreamIdInGoAway) {
  GoAwayFrame frame;
  frame.stream_id = 1 << 30;
  std::unique_ptr<char[]> buffer;
  uint64_t length = HttpEncoder::SerializeGoAwayFrame(frame, &buffer);
  EXPECT_CALL(visitor_, OnGoAwayFrame(frame));
  EXPECT_GT(length, 0u);
  EXPECT_EQ(length, decoder_.ProcessInput(buffer.get(), length));
  EXPECT_THAT(decoder_.error(), IsQuicNoError());
  EXPECT_EQ("", decoder_.error_detail());
}

TEST_F(HttpDecoderTest, PriorityUpdateFrame) {
  InSequence s;
  std::string input1 = quiche::QuicheTextUtils::HexDecode(
      "0f"    // type (PRIORITY_UPDATE)
      "02"    // length
      "00"    // prioritized element type: REQUEST_STREAM
      "03");  // prioritized element id

  PriorityUpdateFrame priority_update1;
  priority_update1.prioritized_element_type = REQUEST_STREAM;
  priority_update1.prioritized_element_id = 0x03;

  // Visitor pauses processing.
  EXPECT_CALL(visitor_, OnPriorityUpdateFrameStart(2)).WillOnce(Return(false));
  quiche::QuicheStringPiece remaining_input(input1);
  QuicByteCount processed_bytes =
      ProcessInputWithGarbageAppended(remaining_input);
  EXPECT_EQ(2u, processed_bytes);
  remaining_input = remaining_input.substr(processed_bytes);

  EXPECT_CALL(visitor_, OnPriorityUpdateFrame(priority_update1))
      .WillOnce(Return(false));
  processed_bytes = ProcessInputWithGarbageAppended(remaining_input);
  EXPECT_EQ(remaining_input.size(), processed_bytes);
  EXPECT_THAT(decoder_.error(), IsQuicNoError());
  EXPECT_EQ("", decoder_.error_detail());

  // Process the full frame.
  EXPECT_CALL(visitor_, OnPriorityUpdateFrameStart(2));
  EXPECT_CALL(visitor_, OnPriorityUpdateFrame(priority_update1));
  EXPECT_EQ(input1.size(), ProcessInput(input1));
  EXPECT_THAT(decoder_.error(), IsQuicNoError());
  EXPECT_EQ("", decoder_.error_detail());

  // Process the frame incrementally.
  EXPECT_CALL(visitor_, OnPriorityUpdateFrameStart(2));
  EXPECT_CALL(visitor_, OnPriorityUpdateFrame(priority_update1));
  ProcessInputCharByChar(input1);
  EXPECT_THAT(decoder_.error(), IsQuicNoError());
  EXPECT_EQ("", decoder_.error_detail());

  std::string input2 = quiche::QuicheTextUtils::HexDecode(
      "0f"        // type (PRIORIRTY)
      "05"        // length
      "80"        // prioritized element type: PUSH_STREAM
      "05"        // prioritized element id
      "666f6f");  // priority field value: "foo"

  PriorityUpdateFrame priority_update2;
  priority_update2.prioritized_element_type = PUSH_STREAM;
  priority_update2.prioritized_element_id = 0x05;
  priority_update2.priority_field_value = "foo";

  // Visitor pauses processing.
  EXPECT_CALL(visitor_, OnPriorityUpdateFrameStart(2)).WillOnce(Return(false));
  remaining_input = input2;
  processed_bytes = ProcessInputWithGarbageAppended(remaining_input);
  EXPECT_EQ(2u, processed_bytes);
  remaining_input = remaining_input.substr(processed_bytes);

  EXPECT_CALL(visitor_, OnPriorityUpdateFrame(priority_update2))
      .WillOnce(Return(false));
  processed_bytes = ProcessInputWithGarbageAppended(remaining_input);
  EXPECT_EQ(remaining_input.size(), processed_bytes);
  EXPECT_THAT(decoder_.error(), IsQuicNoError());
  EXPECT_EQ("", decoder_.error_detail());

  // Process the full frame.
  EXPECT_CALL(visitor_, OnPriorityUpdateFrameStart(2));
  EXPECT_CALL(visitor_, OnPriorityUpdateFrame(priority_update2));
  EXPECT_EQ(input2.size(), ProcessInput(input2));
  EXPECT_THAT(decoder_.error(), IsQuicNoError());
  EXPECT_EQ("", decoder_.error_detail());

  // Process the frame incrementally.
  EXPECT_CALL(visitor_, OnPriorityUpdateFrameStart(2));
  EXPECT_CALL(visitor_, OnPriorityUpdateFrame(priority_update2));
  ProcessInputCharByChar(input2);
  EXPECT_THAT(decoder_.error(), IsQuicNoError());
  EXPECT_EQ("", decoder_.error_detail());
}

TEST_F(HttpDecoderTest, CorruptPriorityUpdateFrame) {
  std::string payload1 = quiche::QuicheTextUtils::HexDecode(
      "80"      // prioritized element type: PUSH_STREAM
      "4005");  // prioritized element id
  std::string payload2 = quiche::QuicheTextUtils::HexDecode(
      "42");  // invalid prioritized element type
  struct {
    const char* const payload;
    size_t payload_length;
    const char* const error_message;
  } kTestData[] = {
      {payload1.data(), 0, "Unable to read prioritized element type."},
      {payload1.data(), 1, "Unable to read prioritized element id."},
      {payload1.data(), 2, "Unable to read prioritized element id."},
      {payload2.data(), 1, "Invalid prioritized element type."},
  };

  for (const auto& test_data : kTestData) {
    std::string input;
    input.push_back(15u);  // type PRIORITY_UPDATE
    input.push_back(test_data.payload_length);
    size_t header_length = input.size();
    input.append(test_data.payload, test_data.payload_length);

    HttpDecoder decoder(&visitor_);
    EXPECT_CALL(visitor_, OnPriorityUpdateFrameStart(header_length));
    EXPECT_CALL(visitor_, OnError(&decoder));

    QuicByteCount processed_bytes =
        decoder.ProcessInput(input.data(), input.size());
    EXPECT_EQ(input.size(), processed_bytes);
    EXPECT_THAT(decoder.error(), IsError(QUIC_HTTP_FRAME_ERROR));
    EXPECT_EQ(test_data.error_message, decoder.error_detail());
  }
}

}  // namespace test

}  // namespace quic
