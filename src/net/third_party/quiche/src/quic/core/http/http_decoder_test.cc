// Copyright (c) 2018 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/third_party/quiche/src/quic/core/http/http_decoder.h"

#include "net/third_party/quiche/src/quic/core/http/http_encoder.h"
#include "net/third_party/quiche/src/quic/core/quic_data_writer.h"
#include "net/third_party/quiche/src/quic/platform/api/quic_arraysize.h"
#include "net/third_party/quiche/src/quic/platform/api/quic_ptr_util.h"
#include "net/third_party/quiche/src/quic/platform/api/quic_str_cat.h"
#include "net/third_party/quiche/src/quic/platform/api/quic_test.h"

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

  MOCK_METHOD1(OnPriorityFrameStart, bool(QuicByteCount header_length));
  MOCK_METHOD1(OnPriorityFrame, bool(const PriorityFrame& frame));
  MOCK_METHOD1(OnCancelPushFrame, bool(const CancelPushFrame& frame));
  MOCK_METHOD1(OnMaxPushIdFrame, bool(const MaxPushIdFrame& frame));
  MOCK_METHOD1(OnGoAwayFrame, bool(const GoAwayFrame& frame));
  MOCK_METHOD1(OnSettingsFrameStart, bool(QuicByteCount header_length));
  MOCK_METHOD1(OnSettingsFrame, bool(const SettingsFrame& frame));
  MOCK_METHOD1(OnDuplicatePushFrame, bool(const DuplicatePushFrame& frame));

  MOCK_METHOD1(OnDataFrameStart, bool(QuicByteCount header_length));
  MOCK_METHOD1(OnDataFramePayload, bool(QuicStringPiece payload));
  MOCK_METHOD0(OnDataFrameEnd, bool());

  MOCK_METHOD1(OnHeadersFrameStart, bool(QuicByteCount header_length));
  MOCK_METHOD1(OnHeadersFramePayload, bool(QuicStringPiece payload));
  MOCK_METHOD0(OnHeadersFrameEnd, bool());

  MOCK_METHOD3(OnPushPromiseFrameStart,
               bool(PushId push_id,
                    QuicByteCount header_length,
                    QuicByteCount push_id_length));
  MOCK_METHOD1(OnPushPromiseFramePayload, bool(QuicStringPiece payload));
  MOCK_METHOD0(OnPushPromiseFrameEnd, bool());

  MOCK_METHOD2(OnUnknownFrameStart, bool(uint64_t, QuicByteCount));
  MOCK_METHOD1(OnUnknownFramePayload, bool(QuicStringPiece));
  MOCK_METHOD0(OnUnknownFrameEnd, bool());
};

class HttpDecoderTest : public QuicTest {
 public:
  HttpDecoderTest() : decoder_(&visitor_) {
    ON_CALL(visitor_, OnPriorityFrameStart(_)).WillByDefault(Return(true));
    ON_CALL(visitor_, OnPriorityFrame(_)).WillByDefault(Return(true));
    ON_CALL(visitor_, OnCancelPushFrame(_)).WillByDefault(Return(true));
    ON_CALL(visitor_, OnMaxPushIdFrame(_)).WillByDefault(Return(true));
    ON_CALL(visitor_, OnGoAwayFrame(_)).WillByDefault(Return(true));
    ON_CALL(visitor_, OnSettingsFrameStart(_)).WillByDefault(Return(true));
    ON_CALL(visitor_, OnSettingsFrame(_)).WillByDefault(Return(true));
    ON_CALL(visitor_, OnDuplicatePushFrame(_)).WillByDefault(Return(true));
    ON_CALL(visitor_, OnDataFrameStart(_)).WillByDefault(Return(true));
    ON_CALL(visitor_, OnDataFramePayload(_)).WillByDefault(Return(true));
    ON_CALL(visitor_, OnDataFrameEnd()).WillByDefault(Return(true));
    ON_CALL(visitor_, OnHeadersFrameStart(_)).WillByDefault(Return(true));
    ON_CALL(visitor_, OnHeadersFramePayload(_)).WillByDefault(Return(true));
    ON_CALL(visitor_, OnHeadersFrameEnd()).WillByDefault(Return(true));
    ON_CALL(visitor_, OnPushPromiseFrameStart(_, _, _))
        .WillByDefault(Return(true));
    ON_CALL(visitor_, OnPushPromiseFramePayload(_)).WillByDefault(Return(true));
    ON_CALL(visitor_, OnPushPromiseFrameEnd()).WillByDefault(Return(true));
    ON_CALL(visitor_, OnUnknownFrameStart(_, _)).WillByDefault(Return(true));
    ON_CALL(visitor_, OnUnknownFramePayload(_)).WillByDefault(Return(true));
    ON_CALL(visitor_, OnUnknownFrameEnd()).WillByDefault(Return(true));
  }
  ~HttpDecoderTest() override = default;

  uint64_t current_frame_type() {
    return HttpDecoderPeer::current_frame_type(&decoder_);
  }

  // Process |input| in a single call to HttpDecoder::ProcessInput().
  QuicByteCount ProcessInput(QuicStringPiece input) {
    return decoder_.ProcessInput(input.data(), input.size());
  }

  // Feed |input| to |decoder_| one character at a time,
  // verifying that each character gets processed.
  void ProcessInputCharByChar(QuicStringPiece input) {
    for (char c : input) {
      EXPECT_EQ(1u, decoder_.ProcessInput(&c, 1));
    }
  }

  // Append garbage to |input|, then process it in a single call to
  // HttpDecoder::ProcessInput().  Verify that garbage is not read.
  QuicByteCount ProcessInputWithGarbageAppended(QuicStringPiece input) {
    std::string input_with_garbage_appended = QuicStrCat(input, "blahblah");
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
  EXPECT_EQ(QUIC_NO_ERROR, decoder_.error());
  EXPECT_EQ("", decoder_.error_detail());
}

TEST_F(HttpDecoderTest, UnknownFrame) {
  std::unique_ptr<char[]> input;

  const QuicByteCount payload_lengths[] = {0, 14, 100};
  const uint64_t frame_types[] = {
      0x21, 0x40, 0x5f, 0x7e, 0x9d,  // some reserved frame types
      0x06, 0x0f, 0x14               // some unknown, not reserved frame types
  };

  for (auto payload_length : payload_lengths) {
    std::string data(payload_length, 'a');

    for (auto frame_type : frame_types) {
      const QuicByteCount total_length =
          QuicDataWriter::GetVarInt62Len(frame_type) +
          QuicDataWriter::GetVarInt62Len(payload_length) + payload_length;
      input = QuicMakeUnique<char[]>(total_length);

      QuicDataWriter writer(total_length, input.get());
      writer.WriteVarInt62(frame_type);
      writer.WriteVarInt62(payload_length);
      const QuicByteCount header_length = writer.length();
      if (payload_length > 0) {
        writer.WriteStringPiece(data);
      }

      EXPECT_CALL(visitor_, OnUnknownFrameStart(frame_type, header_length));
      if (payload_length > 0) {
        EXPECT_CALL(visitor_, OnUnknownFramePayload(Eq(data)));
      }
      EXPECT_CALL(visitor_, OnUnknownFrameEnd());

      EXPECT_EQ(total_length, decoder_.ProcessInput(input.get(), total_length));

      EXPECT_EQ(QUIC_NO_ERROR, decoder_.error());
      ASSERT_EQ("", decoder_.error_detail());
      EXPECT_EQ(frame_type, current_frame_type());
    }
  }
}

TEST_F(HttpDecoderTest, CancelPush) {
  InSequence s;
  std::string input =
      "\x03"   // type (CANCEL_PUSH)
      "\x01"   // length
      "\x01";  // Push Id

  // Visitor pauses processing.
  EXPECT_CALL(visitor_, OnCancelPushFrame(CancelPushFrame({1})))
      .WillOnce(Return(false));
  EXPECT_EQ(input.size(), ProcessInputWithGarbageAppended(input));
  EXPECT_EQ(QUIC_NO_ERROR, decoder_.error());
  EXPECT_EQ("", decoder_.error_detail());

  // Process the full frame.
  EXPECT_CALL(visitor_, OnCancelPushFrame(CancelPushFrame({1})));
  EXPECT_EQ(input.size(), ProcessInput(input));
  EXPECT_EQ(QUIC_NO_ERROR, decoder_.error());
  EXPECT_EQ("", decoder_.error_detail());

  // Process the frame incrementally.
  EXPECT_CALL(visitor_, OnCancelPushFrame(CancelPushFrame({1})));
  ProcessInputCharByChar(input);
  EXPECT_EQ(QUIC_NO_ERROR, decoder_.error());
  EXPECT_EQ("", decoder_.error_detail());
}

TEST_F(HttpDecoderTest, PushPromiseFrame) {
  InSequence s;
  std::string input(
      "\x05"  // type (PUSH PROMISE)
      "\x0f"  // length
      "\xC0"
      "\x00"
      "\x00"
      "\x00"
      "\x00"
      "\x00"
      "\x01"
      "\x01"  // push id 257.
      "Headers",
      17);

  // Visitor pauses processing.
  EXPECT_CALL(visitor_, OnPushPromiseFrameStart(257, 2, 8))
      .WillOnce(Return(false));
  QuicStringPiece remaining_input(input);
  QuicByteCount processed_bytes =
      ProcessInputWithGarbageAppended(remaining_input);
  EXPECT_EQ(10u, processed_bytes);
  remaining_input = remaining_input.substr(processed_bytes);

  EXPECT_CALL(visitor_, OnPushPromiseFramePayload(QuicStringPiece("Headers")))
      .WillOnce(Return(false));
  processed_bytes = ProcessInputWithGarbageAppended(remaining_input);
  EXPECT_EQ(remaining_input.size(), processed_bytes);

  EXPECT_CALL(visitor_, OnPushPromiseFrameEnd()).WillOnce(Return(false));
  EXPECT_EQ(0u, ProcessInputWithGarbageAppended(""));
  EXPECT_EQ(QUIC_NO_ERROR, decoder_.error());
  EXPECT_EQ("", decoder_.error_detail());

  // Process the full frame.
  EXPECT_CALL(visitor_, OnPushPromiseFrameStart(257, 2, 8));
  EXPECT_CALL(visitor_, OnPushPromiseFramePayload(QuicStringPiece("Headers")));
  EXPECT_CALL(visitor_, OnPushPromiseFrameEnd());
  EXPECT_EQ(input.size(), ProcessInput(input));
  EXPECT_EQ(QUIC_NO_ERROR, decoder_.error());
  EXPECT_EQ("", decoder_.error_detail());

  // Process the frame incrementally.
  EXPECT_CALL(visitor_, OnPushPromiseFrameStart(257, 2, 8));
  EXPECT_CALL(visitor_, OnPushPromiseFramePayload(QuicStringPiece("H")));
  EXPECT_CALL(visitor_, OnPushPromiseFramePayload(QuicStringPiece("e")));
  EXPECT_CALL(visitor_, OnPushPromiseFramePayload(QuicStringPiece("a")));
  EXPECT_CALL(visitor_, OnPushPromiseFramePayload(QuicStringPiece("d")));
  EXPECT_CALL(visitor_, OnPushPromiseFramePayload(QuicStringPiece("e")));
  EXPECT_CALL(visitor_, OnPushPromiseFramePayload(QuicStringPiece("r")));
  EXPECT_CALL(visitor_, OnPushPromiseFramePayload(QuicStringPiece("s")));
  EXPECT_CALL(visitor_, OnPushPromiseFrameEnd());
  ProcessInputCharByChar(input);
  EXPECT_EQ(QUIC_NO_ERROR, decoder_.error());
  EXPECT_EQ("", decoder_.error_detail());

  // Process push id incrementally and append headers with last byte of push id.
  EXPECT_CALL(visitor_, OnPushPromiseFrameStart(257, 2, 8));
  EXPECT_CALL(visitor_, OnPushPromiseFramePayload(QuicStringPiece("Headers")));
  EXPECT_CALL(visitor_, OnPushPromiseFrameEnd());
  ProcessInputCharByChar(input.substr(0, 9));
  EXPECT_EQ(8u, ProcessInput(input.substr(9)));
  EXPECT_EQ(QUIC_NO_ERROR, decoder_.error());
  EXPECT_EQ("", decoder_.error_detail());
}

TEST_F(HttpDecoderTest, MaxPushId) {
  InSequence s;
  std::string input =
      "\x0D"   // type (MAX_PUSH_ID)
      "\x01"   // length
      "\x01";  // Push Id

  // Visitor pauses processing.
  EXPECT_CALL(visitor_, OnMaxPushIdFrame(MaxPushIdFrame({1})))
      .WillOnce(Return(false));
  EXPECT_EQ(input.size(), ProcessInputWithGarbageAppended(input));
  EXPECT_EQ(QUIC_NO_ERROR, decoder_.error());
  EXPECT_EQ("", decoder_.error_detail());

  // Process the full frame.
  EXPECT_CALL(visitor_, OnMaxPushIdFrame(MaxPushIdFrame({1})));
  EXPECT_EQ(input.size(), ProcessInput(input));
  EXPECT_EQ(QUIC_NO_ERROR, decoder_.error());
  EXPECT_EQ("", decoder_.error_detail());

  // Process the frame incrementally.
  EXPECT_CALL(visitor_, OnMaxPushIdFrame(MaxPushIdFrame({1})));
  ProcessInputCharByChar(input);
  EXPECT_EQ(QUIC_NO_ERROR, decoder_.error());
  EXPECT_EQ("", decoder_.error_detail());
}

TEST_F(HttpDecoderTest, DuplicatePush) {
  InSequence s;
  std::string input =
      "\x0E"   // type (DUPLICATE_PUSH)
      "\x01"   // length
      "\x01";  // Push Id

  // Visitor pauses processing.
  EXPECT_CALL(visitor_, OnDuplicatePushFrame(DuplicatePushFrame({1})))
      .WillOnce(Return(false));
  EXPECT_EQ(input.size(), ProcessInputWithGarbageAppended(input));
  EXPECT_EQ(QUIC_NO_ERROR, decoder_.error());
  EXPECT_EQ("", decoder_.error_detail());

  // Process the full frame.
  EXPECT_CALL(visitor_, OnDuplicatePushFrame(DuplicatePushFrame({1})));
  EXPECT_EQ(input.size(), ProcessInput(input));
  EXPECT_EQ(QUIC_NO_ERROR, decoder_.error());
  EXPECT_EQ("", decoder_.error_detail());

  // Process the frame incrementally.
  EXPECT_CALL(visitor_, OnDuplicatePushFrame(DuplicatePushFrame({1})));
  ProcessInputCharByChar(input);
  EXPECT_EQ(QUIC_NO_ERROR, decoder_.error());
  EXPECT_EQ("", decoder_.error_detail());
}

TEST_F(HttpDecoderTest, PriorityFrame) {
  InSequence s;
  std::string input =
      "\x02"   // type (PRIORITY)
      "\x04"   // length
      "\x08"   // request stream, request stream, exclusive
      "\x03"   // prioritized_element_id
      "\x04"   // element_dependency_id
      "\xFF";  // weight

  PriorityFrame frame;
  frame.prioritized_type = REQUEST_STREAM;
  frame.dependency_type = REQUEST_STREAM;
  frame.exclusive = true;
  frame.prioritized_element_id = 0x03;
  frame.element_dependency_id = 0x04;
  frame.weight = 0xFF;

  // Visitor pauses processing.
  EXPECT_CALL(visitor_, OnPriorityFrameStart(2)).WillOnce(Return(false));
  QuicStringPiece remaining_input(input);
  QuicByteCount processed_bytes =
      ProcessInputWithGarbageAppended(remaining_input);
  EXPECT_EQ(2u, processed_bytes);
  remaining_input = remaining_input.substr(processed_bytes);

  EXPECT_CALL(visitor_, OnPriorityFrame(frame)).WillOnce(Return(false));
  processed_bytes = ProcessInputWithGarbageAppended(remaining_input);
  EXPECT_EQ(remaining_input.size(), processed_bytes);
  EXPECT_EQ(QUIC_NO_ERROR, decoder_.error());
  EXPECT_EQ("", decoder_.error_detail());

  // Process the full frame.
  EXPECT_CALL(visitor_, OnPriorityFrameStart(2));
  EXPECT_CALL(visitor_, OnPriorityFrame(frame));
  EXPECT_EQ(input.size(), ProcessInput(input));
  EXPECT_EQ(QUIC_NO_ERROR, decoder_.error());
  EXPECT_EQ("", decoder_.error_detail());

  // Process the frame incrementally.
  EXPECT_CALL(visitor_, OnPriorityFrameStart(2));
  EXPECT_CALL(visitor_, OnPriorityFrame(frame));
  ProcessInputCharByChar(input);
  EXPECT_EQ(QUIC_NO_ERROR, decoder_.error());
  EXPECT_EQ("", decoder_.error_detail());

  std::string input2 =
      "\x02"   // type (PRIORITY)
      "\x02"   // length
      "\xf8"   // root of tree, root of tree, exclusive
      "\xFF";  // weight
  PriorityFrame frame2;
  frame2.prioritized_type = ROOT_OF_TREE;
  frame2.dependency_type = ROOT_OF_TREE;
  frame2.exclusive = true;
  frame2.weight = 0xFF;

  EXPECT_CALL(visitor_, OnPriorityFrameStart(2));
  EXPECT_CALL(visitor_, OnPriorityFrame(frame2));
  EXPECT_EQ(input2.size(), ProcessInput(input2));
  EXPECT_EQ(QUIC_NO_ERROR, decoder_.error());
  EXPECT_EQ("", decoder_.error_detail());
}

// Regression test for https://crbug.com/981291 and https://crbug.com/981646.
TEST_F(HttpDecoderTest, CorruptPriorityFrame) {
  const char* const payload1 =
      "\x01"   // request stream, request stream, exclusive
      "\x03"   // prioritized_element_id
      "\x04"   // element_dependency_id
      "\xFF"   // weight
      "\xFF";  // superfluous data
  const char* const payload2 =
      "\xf1"   // root of tree, root of tree, exclusive
      "\xFF"   // weight
      "\xFF";  // superfluous data
  struct {
    const char* const payload;
    size_t payload_length;
    const char* const error_message;
  } kTestData[] = {
      {payload1, 0, "Unable to read PRIORITY frame flags."},
      {payload1, 1, "Unable to read prioritized_element_id."},
      {payload1, 2, "Unable to read element_dependency_id."},
      {payload1, 3, "Unable to read PRIORITY frame weight."},
      {payload1, 5, "Superfluous data in PRIORITY frame."},
      {payload2, 0, "Unable to read PRIORITY frame flags."},
      {payload2, 1, "Unable to read PRIORITY frame weight."},
      {payload2, 3, "Superfluous data in PRIORITY frame."},
  };

  for (const auto& test_data : kTestData) {
    std::string input;
    input.push_back(2u);  // type PRIORITY
    input.push_back(test_data.payload_length);
    size_t header_length = input.size();
    input.append(test_data.payload, test_data.payload_length);

    HttpDecoder decoder(&visitor_);
    EXPECT_CALL(visitor_, OnPriorityFrameStart(header_length));
    EXPECT_CALL(visitor_, OnError(&decoder));

    QuicByteCount processed_bytes =
        decoder.ProcessInput(input.data(), input.size());
    EXPECT_EQ(input.size(), processed_bytes);
    EXPECT_EQ(QUIC_INVALID_FRAME_DATA, decoder.error());
    EXPECT_EQ(test_data.error_message, decoder.error_detail());
  }
}

TEST_F(HttpDecoderTest, SettingsFrame) {
  InSequence s;
  std::string input(
      "\x04"      // type (SETTINGS)
      "\x07"      // length
      "\x03"      // identifier (SETTINGS_NUM_PLACEHOLDERS)
      "\x02"      // content
      "\x06"      // identifier (SETTINGS_MAX_HEADER_LIST_SIZE)
      "\x05"      // content
      "\x41\x00"  // identifier, encoded on 2 bytes (0x40), value is 256 (0x100)
      "\x04",     // content
      9);         // length of string

  SettingsFrame frame;
  frame.values[3] = 2;
  frame.values[6] = 5;
  frame.values[256] = 4;

  // Visitor pauses processing.
  QuicStringPiece remaining_input(input);
  EXPECT_CALL(visitor_, OnSettingsFrameStart(2)).WillOnce(Return(false));
  QuicByteCount processed_bytes =
      ProcessInputWithGarbageAppended(remaining_input);
  EXPECT_EQ(2u, processed_bytes);
  remaining_input = remaining_input.substr(processed_bytes);

  EXPECT_CALL(visitor_, OnSettingsFrame(frame)).WillOnce(Return(false));
  processed_bytes = ProcessInputWithGarbageAppended(remaining_input);
  EXPECT_EQ(remaining_input.size(), processed_bytes);
  EXPECT_EQ(QUIC_NO_ERROR, decoder_.error());
  EXPECT_EQ("", decoder_.error_detail());

  // Process the full frame.
  EXPECT_CALL(visitor_, OnSettingsFrameStart(2));
  EXPECT_CALL(visitor_, OnSettingsFrame(frame));
  EXPECT_EQ(input.size(), ProcessInput(input));
  EXPECT_EQ(QUIC_NO_ERROR, decoder_.error());
  EXPECT_EQ("", decoder_.error_detail());

  // Process the frame incrementally.
  EXPECT_CALL(visitor_, OnSettingsFrameStart(2));
  EXPECT_CALL(visitor_, OnSettingsFrame(frame));
  ProcessInputCharByChar(input);
  EXPECT_EQ(QUIC_NO_ERROR, decoder_.error());
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
      {1, "Unable to read settings frame identifier"},
      {5, "Unable to read settings frame content"},
      {7, "Unable to read settings frame identifier"},
      {12, "Unable to read settings frame content"},
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
    EXPECT_EQ(QUIC_INVALID_FRAME_DATA, decoder.error());
    EXPECT_EQ(test_data.error_message, decoder.error_detail());
  }
}

TEST_F(HttpDecoderTest, DuplicateSettingsIdentifier) {
  std::string input =
      "\x04"   // type (SETTINGS)
      "\x04"   // length
      "\x01"   // identifier
      "\x01"   // content
      "\x01"   // identifier
      "\x02";  // content

  EXPECT_CALL(visitor_, OnSettingsFrameStart(2));
  EXPECT_CALL(visitor_, OnError(&decoder_));

  EXPECT_EQ(input.size(), ProcessInput(input));

  EXPECT_EQ(QUIC_INVALID_FRAME_DATA, decoder_.error());
  EXPECT_EQ("Duplicate SETTINGS identifier.", decoder_.error_detail());
}

TEST_F(HttpDecoderTest, DataFrame) {
  InSequence s;
  std::string input(
      "\x00"    // type (DATA)
      "\x05"    // length
      "Data!",  // data
      7);

  // Visitor pauses processing.
  EXPECT_CALL(visitor_, OnDataFrameStart(2)).WillOnce(Return(false));
  QuicStringPiece remaining_input(input);
  QuicByteCount processed_bytes =
      ProcessInputWithGarbageAppended(remaining_input);
  EXPECT_EQ(2u, processed_bytes);
  remaining_input = remaining_input.substr(processed_bytes);

  EXPECT_CALL(visitor_, OnDataFramePayload(QuicStringPiece("Data!")))
      .WillOnce(Return(false));
  processed_bytes = ProcessInputWithGarbageAppended(remaining_input);
  EXPECT_EQ(remaining_input.size(), processed_bytes);

  EXPECT_CALL(visitor_, OnDataFrameEnd()).WillOnce(Return(false));
  EXPECT_EQ(0u, ProcessInputWithGarbageAppended(""));
  EXPECT_EQ(QUIC_NO_ERROR, decoder_.error());
  EXPECT_EQ("", decoder_.error_detail());

  // Process the full frame.
  EXPECT_CALL(visitor_, OnDataFrameStart(2));
  EXPECT_CALL(visitor_, OnDataFramePayload(QuicStringPiece("Data!")));
  EXPECT_CALL(visitor_, OnDataFrameEnd());
  EXPECT_EQ(input.size(), ProcessInput(input));
  EXPECT_EQ(QUIC_NO_ERROR, decoder_.error());
  EXPECT_EQ("", decoder_.error_detail());

  // Process the frame incrementally.
  EXPECT_CALL(visitor_, OnDataFrameStart(2));
  EXPECT_CALL(visitor_, OnDataFramePayload(QuicStringPiece("D")));
  EXPECT_CALL(visitor_, OnDataFramePayload(QuicStringPiece("a")));
  EXPECT_CALL(visitor_, OnDataFramePayload(QuicStringPiece("t")));
  EXPECT_CALL(visitor_, OnDataFramePayload(QuicStringPiece("a")));
  EXPECT_CALL(visitor_, OnDataFramePayload(QuicStringPiece("!")));
  EXPECT_CALL(visitor_, OnDataFrameEnd());
  ProcessInputCharByChar(input);
  EXPECT_EQ(QUIC_NO_ERROR, decoder_.error());
  EXPECT_EQ("", decoder_.error_detail());
}

TEST_F(HttpDecoderTest, FrameHeaderPartialDelivery) {
  InSequence s;
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
  EXPECT_CALL(visitor_, OnDataFrameStart(3));
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
  // Use a reserved type that takes four bytes as a varint.
  const uint64_t frame_type = 0x1f * 0x222 + 0x21;
  const QuicByteCount payload_length = 0;
  const QuicByteCount header_length =
      QuicDataWriter::GetVarInt62Len(frame_type) +
      QuicDataWriter::GetVarInt62Len(payload_length);

  auto input = QuicMakeUnique<char[]>(header_length);
  QuicDataWriter writer(header_length, input.get());
  writer.WriteVarInt62(frame_type);
  writer.WriteVarInt62(payload_length);

  EXPECT_CALL(visitor_, OnUnknownFrameStart(frame_type, header_length));
  EXPECT_CALL(visitor_, OnUnknownFrameEnd());

  auto raw_input = input.get();
  for (uint64_t i = 0; i < header_length; ++i) {
    char c = raw_input[i];
    EXPECT_EQ(1u, decoder_.ProcessInput(&c, 1));
  }

  EXPECT_EQ(QUIC_NO_ERROR, decoder_.error());
  EXPECT_EQ("", decoder_.error_detail());
  EXPECT_EQ(frame_type, current_frame_type());
}

TEST_F(HttpDecoderTest, GoAway) {
  InSequence s;
  std::string input =
      "\x07"   // type (GOAWAY)
      "\x01"   // length
      "\x01";  // StreamId

  // Visitor pauses processing.
  EXPECT_CALL(visitor_, OnGoAwayFrame(GoAwayFrame({1})))
      .WillOnce(Return(false));
  EXPECT_EQ(input.size(), ProcessInputWithGarbageAppended(input));
  EXPECT_EQ(QUIC_NO_ERROR, decoder_.error());
  EXPECT_EQ("", decoder_.error_detail());

  // Process the full frame.
  EXPECT_CALL(visitor_, OnGoAwayFrame(GoAwayFrame({1})));
  EXPECT_EQ(input.size(), ProcessInput(input));
  EXPECT_EQ(QUIC_NO_ERROR, decoder_.error());
  EXPECT_EQ("", decoder_.error_detail());

  // Process the frame incrementally.
  EXPECT_CALL(visitor_, OnGoAwayFrame(GoAwayFrame({1})));
  ProcessInputCharByChar(input);
  EXPECT_EQ(QUIC_NO_ERROR, decoder_.error());
  EXPECT_EQ("", decoder_.error_detail());
}

TEST_F(HttpDecoderTest, HeadersFrame) {
  InSequence s;
  std::string input =
      "\x01"      // type (HEADERS)
      "\x07"      // length
      "Headers";  // headers

  // Visitor pauses processing.
  EXPECT_CALL(visitor_, OnHeadersFrameStart(2)).WillOnce(Return(false));
  QuicStringPiece remaining_input(input);
  QuicByteCount processed_bytes =
      ProcessInputWithGarbageAppended(remaining_input);
  EXPECT_EQ(2u, processed_bytes);
  remaining_input = remaining_input.substr(processed_bytes);

  EXPECT_CALL(visitor_, OnHeadersFramePayload(QuicStringPiece("Headers")))
      .WillOnce(Return(false));
  processed_bytes = ProcessInputWithGarbageAppended(remaining_input);
  EXPECT_EQ(remaining_input.size(), processed_bytes);

  EXPECT_CALL(visitor_, OnHeadersFrameEnd()).WillOnce(Return(false));
  EXPECT_EQ(0u, ProcessInputWithGarbageAppended(""));
  EXPECT_EQ(QUIC_NO_ERROR, decoder_.error());
  EXPECT_EQ("", decoder_.error_detail());

  // Process the full frame.
  EXPECT_CALL(visitor_, OnHeadersFrameStart(2));
  EXPECT_CALL(visitor_, OnHeadersFramePayload(QuicStringPiece("Headers")));
  EXPECT_CALL(visitor_, OnHeadersFrameEnd());
  EXPECT_EQ(input.size(), ProcessInput(input));
  EXPECT_EQ(QUIC_NO_ERROR, decoder_.error());
  EXPECT_EQ("", decoder_.error_detail());

  // Process the frame incrementally.
  EXPECT_CALL(visitor_, OnHeadersFrameStart(2));
  EXPECT_CALL(visitor_, OnHeadersFramePayload(QuicStringPiece("H")));
  EXPECT_CALL(visitor_, OnHeadersFramePayload(QuicStringPiece("e")));
  EXPECT_CALL(visitor_, OnHeadersFramePayload(QuicStringPiece("a")));
  EXPECT_CALL(visitor_, OnHeadersFramePayload(QuicStringPiece("d")));
  EXPECT_CALL(visitor_, OnHeadersFramePayload(QuicStringPiece("e")));
  EXPECT_CALL(visitor_, OnHeadersFramePayload(QuicStringPiece("r")));
  EXPECT_CALL(visitor_, OnHeadersFramePayload(QuicStringPiece("s")));
  EXPECT_CALL(visitor_, OnHeadersFrameEnd());
  ProcessInputCharByChar(input);
  EXPECT_EQ(QUIC_NO_ERROR, decoder_.error());
  EXPECT_EQ("", decoder_.error_detail());
}

TEST_F(HttpDecoderTest, EmptyDataFrame) {
  InSequence s;
  std::string input(
      "\x00"   // type (DATA)
      "\x00",  // length
      2);

  // Visitor pauses processing.
  EXPECT_CALL(visitor_, OnDataFrameStart(2)).WillOnce(Return(false));
  EXPECT_EQ(input.size(), ProcessInputWithGarbageAppended(input));

  EXPECT_CALL(visitor_, OnDataFrameEnd()).WillOnce(Return(false));
  EXPECT_EQ(0u, ProcessInputWithGarbageAppended(""));
  EXPECT_EQ(QUIC_NO_ERROR, decoder_.error());
  EXPECT_EQ("", decoder_.error_detail());

  // Process the full frame.
  EXPECT_CALL(visitor_, OnDataFrameStart(2));
  EXPECT_CALL(visitor_, OnDataFrameEnd());
  EXPECT_EQ(input.size(), ProcessInput(input));
  EXPECT_EQ(QUIC_NO_ERROR, decoder_.error());
  EXPECT_EQ("", decoder_.error_detail());

  // Process the frame incrementally.
  EXPECT_CALL(visitor_, OnDataFrameStart(2));
  EXPECT_CALL(visitor_, OnDataFrameEnd());
  ProcessInputCharByChar(input);
  EXPECT_EQ(QUIC_NO_ERROR, decoder_.error());
  EXPECT_EQ("", decoder_.error_detail());
}

TEST_F(HttpDecoderTest, EmptyHeadersFrame) {
  InSequence s;
  std::string input(
      "\x01"   // type (HEADERS)
      "\x00",  // length
      2);

  // Visitor pauses processing.
  EXPECT_CALL(visitor_, OnHeadersFrameStart(2)).WillOnce(Return(false));
  EXPECT_EQ(input.size(), ProcessInputWithGarbageAppended(input));

  EXPECT_CALL(visitor_, OnHeadersFrameEnd()).WillOnce(Return(false));
  EXPECT_EQ(0u, ProcessInputWithGarbageAppended(""));
  EXPECT_EQ(QUIC_NO_ERROR, decoder_.error());
  EXPECT_EQ("", decoder_.error_detail());

  // Process the full frame.
  EXPECT_CALL(visitor_, OnHeadersFrameStart(2));
  EXPECT_CALL(visitor_, OnHeadersFrameEnd());
  EXPECT_EQ(input.size(), ProcessInput(input));
  EXPECT_EQ(QUIC_NO_ERROR, decoder_.error());
  EXPECT_EQ("", decoder_.error_detail());

  // Process the frame incrementally.
  EXPECT_CALL(visitor_, OnHeadersFrameStart(2));
  EXPECT_CALL(visitor_, OnHeadersFrameEnd());
  ProcessInputCharByChar(input);
  EXPECT_EQ(QUIC_NO_ERROR, decoder_.error());
  EXPECT_EQ("", decoder_.error_detail());
}

TEST_F(HttpDecoderTest, PushPromiseFrameNoHeaders) {
  InSequence s;
  std::string input =
      "\x05"   // type (PUSH_PROMISE)
      "\x01"   // length
      "\x01";  // Push Id

  // Visitor pauses processing.
  EXPECT_CALL(visitor_, OnPushPromiseFrameStart(1, 2, 1))
      .WillOnce(Return(false));
  EXPECT_EQ(input.size(), ProcessInputWithGarbageAppended(input));

  EXPECT_CALL(visitor_, OnPushPromiseFrameEnd()).WillOnce(Return(false));
  EXPECT_EQ(0u, ProcessInputWithGarbageAppended(""));
  EXPECT_EQ(QUIC_NO_ERROR, decoder_.error());
  EXPECT_EQ("", decoder_.error_detail());

  // Process the full frame.
  EXPECT_CALL(visitor_, OnPushPromiseFrameStart(1, 2, 1));
  EXPECT_CALL(visitor_, OnPushPromiseFrameEnd());
  EXPECT_EQ(input.size(), ProcessInput(input));
  EXPECT_EQ(QUIC_NO_ERROR, decoder_.error());
  EXPECT_EQ("", decoder_.error_detail());

  // Process the frame incrementally.
  EXPECT_CALL(visitor_, OnPushPromiseFrameStart(1, 2, 1));
  EXPECT_CALL(visitor_, OnPushPromiseFrameEnd());
  ProcessInputCharByChar(input);
  EXPECT_EQ(QUIC_NO_ERROR, decoder_.error());
  EXPECT_EQ("", decoder_.error_detail());
}

TEST_F(HttpDecoderTest, MalformedFrameWithOverlyLargePayload) {
  std::string input =
      "\x03"   // type (CANCEL_PUSH)
      "\x10"   // length
      "\x15";  // malformed payload
  // Process the full frame.
  EXPECT_CALL(visitor_, OnError(&decoder_));
  EXPECT_EQ(2u, ProcessInput(input));
  EXPECT_EQ(QUIC_INVALID_FRAME_DATA, decoder_.error());
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
  EXPECT_EQ(5u, decoder_.ProcessInput(input, QUIC_ARRAYSIZE(input)));
  EXPECT_EQ(QUIC_INVALID_FRAME_DATA, decoder_.error());
  EXPECT_EQ("Frame is too large", decoder_.error_detail());
}

TEST_F(HttpDecoderTest, HeadersPausedThenData) {
  InSequence s;
  std::string input(
      "\x01"     // type (HEADERS)
      "\x07"     // length
      "Headers"  // headers
      "\x00"     // type (DATA)
      "\x05"     // length
      "Data!",   // data
      16);

  // Visitor pauses processing, maybe because header decompression is blocked.
  EXPECT_CALL(visitor_, OnHeadersFrameStart(2));
  EXPECT_CALL(visitor_, OnHeadersFramePayload(QuicStringPiece("Headers")));
  EXPECT_CALL(visitor_, OnHeadersFrameEnd()).WillOnce(Return(false));
  QuicStringPiece remaining_input(input);
  QuicByteCount processed_bytes =
      ProcessInputWithGarbageAppended(remaining_input);
  EXPECT_EQ(9u, processed_bytes);
  remaining_input = remaining_input.substr(processed_bytes);

  // Process DATA frame.
  EXPECT_CALL(visitor_, OnDataFrameStart(2));
  EXPECT_CALL(visitor_, OnDataFramePayload(QuicStringPiece("Data!")));
  EXPECT_CALL(visitor_, OnDataFrameEnd());

  processed_bytes = ProcessInput(remaining_input);
  EXPECT_EQ(remaining_input.size(), processed_bytes);

  EXPECT_EQ(QUIC_NO_ERROR, decoder_.error());
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
                    "Unable to read push_id"},
                   {"\x03"  // type (CANCEL_PUSH)
                    "\x04"  // length
                    "\x05"  // valid push id
                    "foo",  // superfluous data
                    "Superfluous data in CANCEL_PUSH frame."},
                   {"\x05"   // type (PUSH_PROMISE)
                    "\x01"   // length
                    "\x40",  // first byte of two-byte varint push id
                    "PUSH_PROMISE frame malformed."},
                   {"\x0D"   // type (MAX_PUSH_ID)
                    "\x01"   // length
                    "\x40",  // first byte of two-byte varint push id
                    "Unable to read push_id"},
                   {"\x0D"  // type (MAX_PUSH_ID)
                    "\x04"  // length
                    "\x05"  // valid push id
                    "foo",  // superfluous data
                    "Superfluous data in MAX_PUSH_ID frame."},
                   {"\x0E"   // type (DUPLICATE_PUSH)
                    "\x01"   // length
                    "\x40",  // first byte of two-byte varint push id
                    "Unable to read push_id"},
                   {"\x0E"  // type (DUPLICATE_PUSH)
                    "\x04"  // length
                    "\x05"  // valid push id
                    "foo",  // superfluous data
                    "Superfluous data in DUPLICATE_PUSH frame."},
                   {"\x07"   // type (GOAWAY)
                    "\x01"   // length
                    "\x40",  // first byte of two-byte varint stream id
                    "Unable to read GOAWAY stream_id"},
                   {"\x07"  // type (GOAWAY)
                    "\x04"  // length
                    "\x05"  // valid stream id
                    "foo",  // superfluous data
                    "Superfluous data in GOAWAY frame."}};

  for (const auto& test_data : kTestData) {
    {
      HttpDecoder decoder(&visitor_);
      EXPECT_CALL(visitor_, OnError(&decoder));

      QuicStringPiece input(test_data.input);
      decoder.ProcessInput(input.data(), input.size());
      EXPECT_EQ(QUIC_INVALID_FRAME_DATA, decoder.error());
      EXPECT_EQ(test_data.error_message, decoder.error_detail());
    }
    {
      HttpDecoder decoder(&visitor_);
      EXPECT_CALL(visitor_, OnError(&decoder));

      QuicStringPiece input(test_data.input);
      for (auto c : input) {
        decoder.ProcessInput(&c, 1);
      }
      EXPECT_EQ(QUIC_INVALID_FRAME_DATA, decoder.error());
      EXPECT_EQ(test_data.error_message, decoder.error_detail());
    }
  }
}  // namespace test

}  // namespace test

}  // namespace quic
