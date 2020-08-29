// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/third_party/quiche/src/http2/decoder/decode_http2_structures.h"

// Tests decoding all of the fixed size HTTP/2 structures (i.e. those defined
// in net/third_party/quiche/src/http2/http2_structures.h).

#include <stddef.h>

#include <string>

#include "testing/gtest/include/gtest/gtest.h"
#include "net/third_party/quiche/src/http2/decoder/decode_buffer.h"
#include "net/third_party/quiche/src/http2/decoder/decode_status.h"
#include "net/third_party/quiche/src/http2/http2_constants.h"
#include "net/third_party/quiche/src/http2/http2_structures_test_util.h"
#include "net/third_party/quiche/src/http2/platform/api/http2_logging.h"
#include "net/third_party/quiche/src/http2/test_tools/http2_random.h"
#include "net/third_party/quiche/src/http2/tools/http2_frame_builder.h"
#include "net/third_party/quiche/src/common/platform/api/quiche_string_piece.h"

using ::testing::AssertionResult;

namespace http2 {
namespace test {
namespace {

template <typename T, size_t N>
quiche::QuicheStringPiece ToStringPiece(T (&data)[N]) {
  return quiche::QuicheStringPiece(reinterpret_cast<const char*>(data),
                                   N * sizeof(T));
}

template <class S>
std::string SerializeStructure(const S& s) {
  Http2FrameBuilder fb;
  fb.Append(s);
  EXPECT_EQ(S::EncodedSize(), fb.size());
  return fb.buffer();
}

template <class S>
class StructureDecoderTest : public ::testing::Test {
 protected:
  typedef S Structure;

  StructureDecoderTest() : random_(), random_decode_count_(100) {}

  // Set the fields of |*p| to random values.
  void Randomize(S* p) { ::http2::test::Randomize(p, &random_); }

  // Fully decodes the Structure at the start of data, and confirms it matches
  // *expected (if provided).
  void DecodeLeadingStructure(const S* expected,
                              quiche::QuicheStringPiece data) {
    ASSERT_LE(S::EncodedSize(), data.size());
    DecodeBuffer db(data);
    Randomize(&structure_);
    DoDecode(&structure_, &db);
    EXPECT_EQ(db.Offset(), S::EncodedSize());
    if (expected != nullptr) {
      EXPECT_EQ(structure_, *expected);
    }
  }

  template <size_t N>
  void DecodeLeadingStructure(const char (&data)[N]) {
    DecodeLeadingStructure(nullptr, quiche::QuicheStringPiece(data, N));
  }

  // Encode the structure |in_s| into bytes, then decode the bytes
  // and validate that the decoder produced the same field values.
  void EncodeThenDecode(const S& in_s) {
    std::string bytes = SerializeStructure(in_s);
    EXPECT_EQ(S::EncodedSize(), bytes.size());
    DecodeLeadingStructure(&in_s, bytes);
  }

  // Generate
  void TestDecodingRandomizedStructures(size_t count) {
    for (size_t i = 0; i < count && !HasFailure(); ++i) {
      Structure input;
      Randomize(&input);
      EncodeThenDecode(input);
    }
  }

  void TestDecodingRandomizedStructures() {
    TestDecodingRandomizedStructures(random_decode_count_);
  }

  Http2Random random_;
  const size_t random_decode_count_;
  uint32_t decode_offset_ = 0;
  S structure_;
  size_t fast_decode_count_ = 0;
  size_t slow_decode_count_ = 0;
};

class FrameHeaderDecoderTest : public StructureDecoderTest<Http2FrameHeader> {};

TEST_F(FrameHeaderDecoderTest, DecodesLiteral) {
  {
    // Realistic input.
    const char kData[] = {
        '\x00', '\x00', '\x05',          // Payload length: 5
        '\x01',                          // Frame type: HEADERS
        '\x08',                          // Flags: PADDED
        '\x00', '\x00', '\x00', '\x01',  // Stream ID: 1
        '\x04',                          // Padding length: 4
        '\x00', '\x00', '\x00', '\x00',  // Padding bytes
    };
    DecodeLeadingStructure(kData);
    if (!HasFailure()) {
      EXPECT_EQ(5u, structure_.payload_length);
      EXPECT_EQ(Http2FrameType::HEADERS, structure_.type);
      EXPECT_EQ(Http2FrameFlag::PADDED, structure_.flags);
      EXPECT_EQ(1u, structure_.stream_id);
    }
  }
  {
    // Unlikely input.
    const char kData[] = {
        '\xff', '\xff', '\xff',          // Payload length: uint24 max
        '\xff',                          // Frame type: Unknown
        '\xff',                          // Flags: Unknown/All
        '\xff', '\xff', '\xff', '\xff',  // Stream ID: uint31 max, plus R-bit
    };
    DecodeLeadingStructure(kData);
    if (!HasFailure()) {
      EXPECT_EQ((1u << 24) - 1, structure_.payload_length);
      EXPECT_EQ(static_cast<Http2FrameType>(255), structure_.type);
      EXPECT_EQ(255, structure_.flags);
      EXPECT_EQ(0x7FFFFFFFu, structure_.stream_id);
    }
  }
}

TEST_F(FrameHeaderDecoderTest, DecodesRandomized) {
  TestDecodingRandomizedStructures();
}

//------------------------------------------------------------------------------

class PriorityFieldsDecoderTest
    : public StructureDecoderTest<Http2PriorityFields> {};

TEST_F(PriorityFieldsDecoderTest, DecodesLiteral) {
  {
    const char kData[] = {
        '\x80', '\x00', '\x00', '\x05',  // Exclusive (yes) and Dependency (5)
        '\xff',                          // Weight: 256 (after adding 1)
    };
    DecodeLeadingStructure(kData);
    if (!HasFailure()) {
      EXPECT_EQ(5u, structure_.stream_dependency);
      EXPECT_EQ(256u, structure_.weight);
      EXPECT_EQ(true, structure_.is_exclusive);
    }
  }
  {
    const char kData[] = {
        '\x7f', '\xff',
        '\xff', '\xff',  // Exclusive (no) and Dependency (0x7fffffff)
        '\x00',          // Weight: 1 (after adding 1)
    };
    DecodeLeadingStructure(kData);
    if (!HasFailure()) {
      EXPECT_EQ(StreamIdMask(), structure_.stream_dependency);
      EXPECT_EQ(1u, structure_.weight);
      EXPECT_FALSE(structure_.is_exclusive);
    }
  }
}

TEST_F(PriorityFieldsDecoderTest, DecodesRandomized) {
  TestDecodingRandomizedStructures();
}

//------------------------------------------------------------------------------

class RstStreamFieldsDecoderTest
    : public StructureDecoderTest<Http2RstStreamFields> {};

TEST_F(RstStreamFieldsDecoderTest, DecodesLiteral) {
  {
    const char kData[] = {
        '\x00', '\x00', '\x00', '\x01',  // Error: PROTOCOL_ERROR
    };
    DecodeLeadingStructure(kData);
    if (!HasFailure()) {
      EXPECT_TRUE(structure_.IsSupportedErrorCode());
      EXPECT_EQ(Http2ErrorCode::PROTOCOL_ERROR, structure_.error_code);
    }
  }
  {
    const char kData[] = {
        '\xff', '\xff', '\xff',
        '\xff',  // Error: max uint32 (Unknown error code)
    };
    DecodeLeadingStructure(kData);
    if (!HasFailure()) {
      EXPECT_FALSE(structure_.IsSupportedErrorCode());
      EXPECT_EQ(static_cast<Http2ErrorCode>(0xffffffff), structure_.error_code);
    }
  }
}

TEST_F(RstStreamFieldsDecoderTest, DecodesRandomized) {
  TestDecodingRandomizedStructures();
}

//------------------------------------------------------------------------------

class SettingFieldsDecoderTest
    : public StructureDecoderTest<Http2SettingFields> {};

TEST_F(SettingFieldsDecoderTest, DecodesLiteral) {
  {
    const char kData[] = {
        '\x00', '\x01',                  // Setting: HEADER_TABLE_SIZE
        '\x00', '\x00', '\x40', '\x00',  // Value: 16K
    };
    DecodeLeadingStructure(kData);
    if (!HasFailure()) {
      EXPECT_TRUE(structure_.IsSupportedParameter());
      EXPECT_EQ(Http2SettingsParameter::HEADER_TABLE_SIZE,
                structure_.parameter);
      EXPECT_EQ(1u << 14, structure_.value);
    }
  }
  {
    const char kData[] = {
        '\x00', '\x00',                  // Setting: Unknown (0)
        '\xff', '\xff', '\xff', '\xff',  // Value: max uint32
    };
    DecodeLeadingStructure(kData);
    if (!HasFailure()) {
      EXPECT_FALSE(structure_.IsSupportedParameter());
      EXPECT_EQ(static_cast<Http2SettingsParameter>(0), structure_.parameter);
    }
  }
}

TEST_F(SettingFieldsDecoderTest, DecodesRandomized) {
  TestDecodingRandomizedStructures();
}

//------------------------------------------------------------------------------

class PushPromiseFieldsDecoderTest
    : public StructureDecoderTest<Http2PushPromiseFields> {};

TEST_F(PushPromiseFieldsDecoderTest, DecodesLiteral) {
  {
    const char kData[] = {
        '\x00', '\x01', '\x8a', '\x92',  // Promised Stream ID: 101010
    };
    DecodeLeadingStructure(kData);
    if (!HasFailure()) {
      EXPECT_EQ(101010u, structure_.promised_stream_id);
    }
  }
  {
    // Promised stream id has R-bit (reserved for future use) set, which
    // should be cleared by the decoder.
    const char kData[] = {
        '\xff', '\xff', '\xff',
        '\xff',  // Promised Stream ID: max uint31 and R-bit
    };
    DecodeLeadingStructure(kData);
    if (!HasFailure()) {
      EXPECT_EQ(StreamIdMask(), structure_.promised_stream_id);
    }
  }
}

TEST_F(PushPromiseFieldsDecoderTest, DecodesRandomized) {
  TestDecodingRandomizedStructures();
}

//------------------------------------------------------------------------------

class PingFieldsDecoderTest : public StructureDecoderTest<Http2PingFields> {};

TEST_F(PingFieldsDecoderTest, DecodesLiteral) {
  {
    // Each byte is different, so can detect if order changed.
    const char kData[] = {
        '\x00', '\x01', '\x02', '\x03', '\x04', '\x05', '\x06', '\x07',
    };
    DecodeLeadingStructure(kData);
    if (!HasFailure()) {
      EXPECT_EQ(quiche::QuicheStringPiece(kData, 8),
                ToStringPiece(structure_.opaque_bytes));
    }
  }
  {
    // All zeros, detect problems handling NULs.
    const char kData[] = {
        '\x00', '\x00', '\x00', '\x00', '\x00', '\x00', '\x00', '\x00',
    };
    DecodeLeadingStructure(kData);
    if (!HasFailure()) {
      EXPECT_EQ(quiche::QuicheStringPiece(kData, 8),
                ToStringPiece(structure_.opaque_bytes));
    }
  }
  {
    const char kData[] = {
        '\xff', '\xff', '\xff', '\xff', '\xff', '\xff', '\xff', '\xff',
    };
    DecodeLeadingStructure(kData);
    if (!HasFailure()) {
      EXPECT_EQ(quiche::QuicheStringPiece(kData, 8),
                ToStringPiece(structure_.opaque_bytes));
    }
  }
}

TEST_F(PingFieldsDecoderTest, DecodesRandomized) {
  TestDecodingRandomizedStructures();
}

//------------------------------------------------------------------------------

class GoAwayFieldsDecoderTest : public StructureDecoderTest<Http2GoAwayFields> {
};

TEST_F(GoAwayFieldsDecoderTest, DecodesLiteral) {
  {
    const char kData[] = {
        '\x00', '\x00', '\x00', '\x00',  // Last Stream ID: 0
        '\x00', '\x00', '\x00', '\x00',  // Error: NO_ERROR (0)
    };
    DecodeLeadingStructure(kData);
    if (!HasFailure()) {
      EXPECT_EQ(0u, structure_.last_stream_id);
      EXPECT_TRUE(structure_.IsSupportedErrorCode());
      EXPECT_EQ(Http2ErrorCode::HTTP2_NO_ERROR, structure_.error_code);
    }
  }
  {
    const char kData[] = {
        '\x00', '\x00', '\x00', '\x01',  // Last Stream ID: 1
        '\x00', '\x00', '\x00', '\x0d',  // Error: HTTP_1_1_REQUIRED
    };
    DecodeLeadingStructure(kData);
    if (!HasFailure()) {
      EXPECT_EQ(1u, structure_.last_stream_id);
      EXPECT_TRUE(structure_.IsSupportedErrorCode());
      EXPECT_EQ(Http2ErrorCode::HTTP_1_1_REQUIRED, structure_.error_code);
    }
  }
  {
    const char kData[] = {
        '\xff', '\xff',
        '\xff', '\xff',  // Last Stream ID: max uint31 and R-bit
        '\xff', '\xff',
        '\xff', '\xff',  // Error: max uint32 (Unknown error code)
    };
    DecodeLeadingStructure(kData);
    if (!HasFailure()) {
      EXPECT_EQ(StreamIdMask(), structure_.last_stream_id);  // No high-bit.
      EXPECT_FALSE(structure_.IsSupportedErrorCode());
      EXPECT_EQ(static_cast<Http2ErrorCode>(0xffffffff), structure_.error_code);
    }
  }
}

TEST_F(GoAwayFieldsDecoderTest, DecodesRandomized) {
  TestDecodingRandomizedStructures();
}

//------------------------------------------------------------------------------

class WindowUpdateFieldsDecoderTest
    : public StructureDecoderTest<Http2WindowUpdateFields> {};

TEST_F(WindowUpdateFieldsDecoderTest, DecodesLiteral) {
  {
    const char kData[] = {
        '\x00', '\x01', '\x00', '\x00',  // Window Size Increment: 2 ^ 16
    };
    DecodeLeadingStructure(kData);
    if (!HasFailure()) {
      EXPECT_EQ(1u << 16, structure_.window_size_increment);
    }
  }
  {
    // Increment must be non-zero, but we need to be able to decode the invalid
    // zero to detect it.
    const char kData[] = {
        '\x00', '\x00', '\x00', '\x00',  // Window Size Increment: 0
    };
    DecodeLeadingStructure(kData);
    if (!HasFailure()) {
      EXPECT_EQ(0u, structure_.window_size_increment);
    }
  }
  {
    // Increment has R-bit (reserved for future use) set, which
    // should be cleared by the decoder.
    // clang-format off
    const char kData[] = {
        // Window Size Increment: max uint31 and R-bit
        '\xff', '\xff', '\xff', '\xff',
    };
    // clang-format on
    DecodeLeadingStructure(kData);
    if (!HasFailure()) {
      EXPECT_EQ(StreamIdMask(), structure_.window_size_increment);
    }
  }
}

TEST_F(WindowUpdateFieldsDecoderTest, DecodesRandomized) {
  TestDecodingRandomizedStructures();
}

//------------------------------------------------------------------------------

class AltSvcFieldsDecoderTest : public StructureDecoderTest<Http2AltSvcFields> {
};

TEST_F(AltSvcFieldsDecoderTest, DecodesLiteral) {
  {
    const char kData[] = {
        '\x00', '\x00',  // Origin Length: 0
    };
    DecodeLeadingStructure(kData);
    if (!HasFailure()) {
      EXPECT_EQ(0, structure_.origin_length);
    }
  }
  {
    const char kData[] = {
        '\x00', '\x14',  // Origin Length: 20
    };
    DecodeLeadingStructure(kData);
    if (!HasFailure()) {
      EXPECT_EQ(20, structure_.origin_length);
    }
  }
  {
    const char kData[] = {
        '\xff', '\xff',  // Origin Length: uint16 max
    };
    DecodeLeadingStructure(kData);
    if (!HasFailure()) {
      EXPECT_EQ(65535, structure_.origin_length);
    }
  }
}

TEST_F(AltSvcFieldsDecoderTest, DecodesRandomized) {
  TestDecodingRandomizedStructures();
}

}  // namespace
}  // namespace test
}  // namespace http2
