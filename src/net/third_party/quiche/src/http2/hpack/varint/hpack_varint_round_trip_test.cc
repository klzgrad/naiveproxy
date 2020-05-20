// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/third_party/quiche/src/http2/hpack/varint/hpack_varint_decoder.h"

// Test HpackVarintDecoder against data encoded via HpackBlockBuilder,
// which uses HpackVarintEncoder under the hood.

#include <stddef.h>

#include <iterator>
#include <set>
#include <vector>

#include "testing/gtest/include/gtest/gtest.h"
#include "net/third_party/quiche/src/http2/hpack/tools/hpack_block_builder.h"
#include "net/third_party/quiche/src/http2/platform/api/http2_logging.h"
#include "net/third_party/quiche/src/http2/platform/api/http2_string_utils.h"
#include "net/third_party/quiche/src/http2/tools/random_decoder_test.h"
#include "net/third_party/quiche/src/common/platform/api/quiche_str_cat.h"
#include "net/third_party/quiche/src/common/platform/api/quiche_string_piece.h"

using ::testing::AssertionFailure;
using ::testing::AssertionSuccess;

namespace http2 {
namespace test {
namespace {

// Returns the highest value with the specified number of extension bytes
// and the specified prefix length (bits).
uint64_t HiValueOfExtensionBytes(uint32_t extension_bytes,
                                 uint32_t prefix_length) {
  return (1 << prefix_length) - 2 +
         (extension_bytes == 0 ? 0 : (1LLU << (extension_bytes * 7)));
}

class HpackVarintRoundTripTest : public RandomDecoderTest {
 protected:
  HpackVarintRoundTripTest() : prefix_length_(0) {}

  DecodeStatus StartDecoding(DecodeBuffer* b) override {
    CHECK_LT(0u, b->Remaining());
    uint8_t prefix = b->DecodeUInt8();
    return decoder_.Start(prefix, prefix_length_, b);
  }

  DecodeStatus ResumeDecoding(DecodeBuffer* b) override {
    return decoder_.Resume(b);
  }

  void DecodeSeveralWays(uint64_t expected_value, uint32_t expected_offset) {
    // The validator is called after each of the several times that the input
    // DecodeBuffer is decoded, each with a different segmentation of the input.
    // Validate that decoder_.value() matches the expected value.
    Validator validator = [expected_value, this](
                              const DecodeBuffer& db,
                              DecodeStatus status) -> AssertionResult {
      if (decoder_.value() != expected_value) {
        return AssertionFailure()
               << "Value doesn't match expected: " << decoder_.value()
               << " != " << expected_value;
      }
      return AssertionSuccess();
    };

    // First validate that decoding is done and that we've advanced the cursor
    // the expected amount.
    validator = ValidateDoneAndOffset(expected_offset, validator);

    // StartDecoding, above, requires the DecodeBuffer be non-empty so that it
    // can call Start with the prefix byte.
    bool return_non_zero_on_first = true;

    DecodeBuffer b(buffer_);
    EXPECT_TRUE(
        DecodeAndValidateSeveralWays(&b, return_non_zero_on_first, validator));

    EXPECT_EQ(expected_value, decoder_.value());
    EXPECT_EQ(expected_offset, b.Offset());
  }

  void EncodeNoRandom(uint64_t value, uint8_t prefix_length) {
    DCHECK_LE(3, prefix_length);
    DCHECK_LE(prefix_length, 8);
    prefix_length_ = prefix_length;

    HpackBlockBuilder bb;
    bb.AppendHighBitsAndVarint(0, prefix_length_, value);
    buffer_ = bb.buffer();
    ASSERT_LT(0u, buffer_.size());

    const uint8_t prefix_mask = (1 << prefix_length_) - 1;
    ASSERT_EQ(static_cast<uint8_t>(buffer_[0]),
              static_cast<uint8_t>(buffer_[0]) & prefix_mask);
  }

  void Encode(uint64_t value, uint8_t prefix_length) {
    EncodeNoRandom(value, prefix_length);
    // Add some random bits to the prefix (the first byte) above the mask.
    uint8_t prefix = buffer_[0];
    buffer_[0] = prefix | (Random().Rand8() << prefix_length);
    const uint8_t prefix_mask = (1 << prefix_length_) - 1;
    ASSERT_EQ(prefix, buffer_[0] & prefix_mask);
  }

  // This is really a test of HpackBlockBuilder, making sure that the input to
  // HpackVarintDecoder is as expected, which also acts as confirmation that
  // my thinking about the encodings being used by the tests, i.e. cover the
  // range desired.
  void ValidateEncoding(uint64_t value,
                        uint64_t minimum,
                        uint64_t maximum,
                        size_t expected_bytes) {
    ASSERT_EQ(expected_bytes, buffer_.size());
    if (expected_bytes > 1) {
      const uint8_t prefix_mask = (1 << prefix_length_) - 1;
      EXPECT_EQ(prefix_mask, buffer_[0] & prefix_mask);
      size_t last = expected_bytes - 1;
      for (size_t ndx = 1; ndx < last; ++ndx) {
        // Before the last extension byte, we expect the high-bit set.
        uint8_t byte = buffer_[ndx];
        if (value == minimum) {
          EXPECT_EQ(0x80, byte) << "ndx=" << ndx;
        } else if (value == maximum) {
          if (expected_bytes < 11) {
            EXPECT_EQ(0xff, byte) << "ndx=" << ndx;
          }
        } else {
          EXPECT_EQ(0x80, byte & 0x80) << "ndx=" << ndx;
        }
      }
      // The last extension byte should not have the high-bit set.
      uint8_t byte = buffer_[last];
      if (value == minimum) {
        if (expected_bytes == 2) {
          EXPECT_EQ(0x00, byte);
        } else {
          EXPECT_EQ(0x01, byte);
        }
      } else if (value == maximum) {
        if (expected_bytes < 11) {
          EXPECT_EQ(0x7f, byte);
        }
      } else {
        EXPECT_EQ(0x00, byte & 0x80);
      }
    } else {
      const uint8_t prefix_mask = (1 << prefix_length_) - 1;
      EXPECT_EQ(value, static_cast<uint32_t>(buffer_[0] & prefix_mask));
      EXPECT_LT(value, prefix_mask);
    }
  }

  void EncodeAndDecodeValues(const std::set<uint64_t>& values,
                             uint8_t prefix_length,
                             size_t expected_bytes) {
    CHECK(!values.empty());
    const uint64_t minimum = *values.begin();
    const uint64_t maximum = *values.rbegin();
    for (const uint64_t value : values) {
      Encode(value, prefix_length);  // Sets buffer_.

      std::string msg = quiche::QuicheStrCat(
          "value=", value, " (0x", Http2Hex(value),
          "), prefix_length=", prefix_length,
          ", expected_bytes=", expected_bytes, "\n", Http2HexDump(buffer_));

      if (value == minimum) {
        HTTP2_LOG(INFO) << "Checking minimum; " << msg;
      } else if (value == maximum) {
        HTTP2_LOG(INFO) << "Checking maximum; " << msg;
      }

      SCOPED_TRACE(msg);
      ValidateEncoding(value, minimum, maximum, expected_bytes);
      DecodeSeveralWays(value, expected_bytes);

      // Append some random data to the end of buffer_ and repeat. That random
      // data should be ignored.
      buffer_.append(Random().RandString(1 + Random().Uniform(10)));
      DecodeSeveralWays(value, expected_bytes);

      // If possible, add extension bytes that don't change the value.
      if (1 < expected_bytes) {
        buffer_.resize(expected_bytes);
        for (uint8_t total_bytes = expected_bytes + 1; total_bytes <= 6;
             ++total_bytes) {
          // Mark the current last byte as not being the last one.
          EXPECT_EQ(0x00, 0x80 & buffer_.back());
          buffer_.back() |= 0x80;
          buffer_.push_back('\0');
          DecodeSeveralWays(value, total_bytes);
        }
      }
    }
  }

  // Encode values (all or some of it) in [start, start+range).  Check
  // that |start| is the smallest value and |start+range-1| is the largest value
  // corresponding to |expected_bytes|, except if |expected_bytes| is maximal.
  void EncodeAndDecodeValuesInRange(uint64_t start,
                                    uint64_t range,
                                    uint8_t prefix_length,
                                    size_t expected_bytes) {
    const uint8_t prefix_mask = (1 << prefix_length) - 1;
    const uint64_t beyond = start + range;

    HTTP2_LOG(INFO)
        << "############################################################";
    HTTP2_LOG(INFO) << "prefix_length=" << static_cast<int>(prefix_length);
    HTTP2_LOG(INFO) << "prefix_mask=" << std::hex
                    << static_cast<int>(prefix_mask);
    HTTP2_LOG(INFO) << "start=" << start << " (" << std::hex << start << ")";
    HTTP2_LOG(INFO) << "range=" << range << " (" << std::hex << range << ")";
    HTTP2_LOG(INFO) << "beyond=" << beyond << " (" << std::hex << beyond << ")";
    HTTP2_LOG(INFO) << "expected_bytes=" << expected_bytes;

    if (expected_bytes < 11) {
      // Confirm the claim that beyond requires more bytes.
      Encode(beyond, prefix_length);
      EXPECT_EQ(expected_bytes + 1, buffer_.size()) << Http2HexDump(buffer_);
    }

    std::set<uint64_t> values;
    if (range < 200) {
      // Select all values in the range.
      for (uint64_t offset = 0; offset < range; ++offset) {
        values.insert(start + offset);
      }
    } else {
      // Select some values in this range, including the minimum and maximum
      // values that require exactly |expected_bytes| extension bytes.
      values.insert({start, start + 1, beyond - 2, beyond - 1});
      while (values.size() < 100) {
        values.insert(Random().UniformInRange(start, beyond - 1));
      }
    }

    EncodeAndDecodeValues(values, prefix_length, expected_bytes);
  }

  HpackVarintDecoder decoder_;
  std::string buffer_;
  uint8_t prefix_length_;
};

// To help me and future debuggers of varint encodings, this HTTP2_LOGs out the
// transition points where a new extension byte is added.
TEST_F(HpackVarintRoundTripTest, Encode) {
  for (int prefix_length = 3; prefix_length <= 8; ++prefix_length) {
    const uint64_t a = HiValueOfExtensionBytes(0, prefix_length);
    const uint64_t b = HiValueOfExtensionBytes(1, prefix_length);
    const uint64_t c = HiValueOfExtensionBytes(2, prefix_length);
    const uint64_t d = HiValueOfExtensionBytes(3, prefix_length);
    const uint64_t e = HiValueOfExtensionBytes(4, prefix_length);
    const uint64_t f = HiValueOfExtensionBytes(5, prefix_length);
    const uint64_t g = HiValueOfExtensionBytes(6, prefix_length);
    const uint64_t h = HiValueOfExtensionBytes(7, prefix_length);
    const uint64_t i = HiValueOfExtensionBytes(8, prefix_length);
    const uint64_t j = HiValueOfExtensionBytes(9, prefix_length);

    HTTP2_LOG(INFO)
        << "############################################################";
    HTTP2_LOG(INFO) << "prefix_length=" << prefix_length << "   a=" << a
                    << "   b=" << b << "   c=" << c << "   d=" << d
                    << "   e=" << e << "   f=" << f << "   g=" << g
                    << "   h=" << h << "   i=" << i << "   j=" << j;

    std::vector<uint64_t> values = {
        0,     1,                       // Force line break.
        a - 1, a, a + 1, a + 2, a + 3,  // Force line break.
        b - 1, b, b + 1, b + 2, b + 3,  // Force line break.
        c - 1, c, c + 1, c + 2, c + 3,  // Force line break.
        d - 1, d, d + 1, d + 2, d + 3,  // Force line break.
        e - 1, e, e + 1, e + 2, e + 3,  // Force line break.
        f - 1, f, f + 1, f + 2, f + 3,  // Force line break.
        g - 1, g, g + 1, g + 2, g + 3,  // Force line break.
        h - 1, h, h + 1, h + 2, h + 3,  // Force line break.
        i - 1, i, i + 1, i + 2, i + 3,  // Force line break.
        j - 1, j, j + 1, j + 2, j + 3,  // Force line break.
    };

    for (uint64_t value : values) {
      EncodeNoRandom(value, prefix_length);
      std::string dump = Http2HexDump(buffer_);
      HTTP2_LOG(INFO) << Http2StringPrintf("%10llu %0#18x ", value, value)
                      << Http2HexDump(buffer_).substr(7);
    }
  }
}

TEST_F(HpackVarintRoundTripTest, FromSpec1337) {
  DecodeBuffer b(quiche::QuicheStringPiece("\x1f\x9a\x0a"));
  uint32_t prefix_length = 5;
  uint8_t p = b.DecodeUInt8();
  EXPECT_EQ(1u, b.Offset());
  EXPECT_EQ(DecodeStatus::kDecodeDone, decoder_.Start(p, prefix_length, &b));
  EXPECT_EQ(3u, b.Offset());
  EXPECT_EQ(1337u, decoder_.value());

  EncodeNoRandom(1337, prefix_length);
  EXPECT_EQ(3u, buffer_.size());
  EXPECT_EQ('\x1f', buffer_[0]);
  EXPECT_EQ('\x9a', buffer_[1]);
  EXPECT_EQ('\x0a', buffer_[2]);
}

// Test all the values that fit into the prefix (one less than the mask).
TEST_F(HpackVarintRoundTripTest, ValidatePrefixOnly) {
  for (int prefix_length = 3; prefix_length <= 8; ++prefix_length) {
    const uint8_t prefix_mask = (1 << prefix_length) - 1;
    EncodeAndDecodeValuesInRange(0, prefix_mask, prefix_length, 1);
  }
}

// Test all values that require exactly 1 extension byte.
TEST_F(HpackVarintRoundTripTest, ValidateOneExtensionByte) {
  for (int prefix_length = 3; prefix_length <= 8; ++prefix_length) {
    const uint64_t start = HiValueOfExtensionBytes(0, prefix_length) + 1;
    EncodeAndDecodeValuesInRange(start, 128, prefix_length, 2);
  }
}

// Test *some* values that require exactly 2 extension bytes.
TEST_F(HpackVarintRoundTripTest, ValidateTwoExtensionBytes) {
  for (int prefix_length = 3; prefix_length <= 8; ++prefix_length) {
    const uint64_t start = HiValueOfExtensionBytes(1, prefix_length) + 1;
    const uint64_t range = 127 << 7;

    EncodeAndDecodeValuesInRange(start, range, prefix_length, 3);
  }
}

// Test *some* values that require 3 extension bytes.
TEST_F(HpackVarintRoundTripTest, ValidateThreeExtensionBytes) {
  for (int prefix_length = 3; prefix_length <= 8; ++prefix_length) {
    const uint64_t start = HiValueOfExtensionBytes(2, prefix_length) + 1;
    const uint64_t range = 127 << 14;

    EncodeAndDecodeValuesInRange(start, range, prefix_length, 4);
  }
}

// Test *some* values that require 4 extension bytes.
TEST_F(HpackVarintRoundTripTest, ValidateFourExtensionBytes) {
  for (int prefix_length = 3; prefix_length <= 8; ++prefix_length) {
    const uint64_t start = HiValueOfExtensionBytes(3, prefix_length) + 1;
    const uint64_t range = 127 << 21;

    EncodeAndDecodeValuesInRange(start, range, prefix_length, 5);
  }
}

// Test *some* values that require 5 extension bytes.
TEST_F(HpackVarintRoundTripTest, ValidateFiveExtensionBytes) {
  for (int prefix_length = 3; prefix_length <= 8; ++prefix_length) {
    const uint64_t start = HiValueOfExtensionBytes(4, prefix_length) + 1;
    const uint64_t range = 127llu << 28;

    EncodeAndDecodeValuesInRange(start, range, prefix_length, 6);
  }
}

// Test *some* values that require 6 extension bytes.
TEST_F(HpackVarintRoundTripTest, ValidateSixExtensionBytes) {
  for (int prefix_length = 3; prefix_length <= 8; ++prefix_length) {
    const uint64_t start = HiValueOfExtensionBytes(5, prefix_length) + 1;
    const uint64_t range = 127llu << 35;

    EncodeAndDecodeValuesInRange(start, range, prefix_length, 7);
  }
}

// Test *some* values that require 7 extension bytes.
TEST_F(HpackVarintRoundTripTest, ValidateSevenExtensionBytes) {
  for (int prefix_length = 3; prefix_length <= 8; ++prefix_length) {
    const uint64_t start = HiValueOfExtensionBytes(6, prefix_length) + 1;
    const uint64_t range = 127llu << 42;

    EncodeAndDecodeValuesInRange(start, range, prefix_length, 8);
  }
}

// Test *some* values that require 8 extension bytes.
TEST_F(HpackVarintRoundTripTest, ValidateEightExtensionBytes) {
  for (int prefix_length = 3; prefix_length <= 8; ++prefix_length) {
    const uint64_t start = HiValueOfExtensionBytes(7, prefix_length) + 1;
    const uint64_t range = 127llu << 49;

    EncodeAndDecodeValuesInRange(start, range, prefix_length, 9);
  }
}

// Test *some* values that require 9 extension bytes.
TEST_F(HpackVarintRoundTripTest, ValidateNineExtensionBytes) {
  for (int prefix_length = 3; prefix_length <= 8; ++prefix_length) {
    const uint64_t start = HiValueOfExtensionBytes(8, prefix_length) + 1;
    const uint64_t range = 127llu << 56;

    EncodeAndDecodeValuesInRange(start, range, prefix_length, 10);
  }
}

// Test *some* values that require 10 extension bytes.
TEST_F(HpackVarintRoundTripTest, ValidateTenExtensionBytes) {
  for (int prefix_length = 3; prefix_length <= 8; ++prefix_length) {
    const uint64_t start = HiValueOfExtensionBytes(9, prefix_length) + 1;
    const uint64_t range = std::numeric_limits<uint64_t>::max() - start;

    EncodeAndDecodeValuesInRange(start, range, prefix_length, 11);
  }
}

}  // namespace
}  // namespace test
}  // namespace http2
