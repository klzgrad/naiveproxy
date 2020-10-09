// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/third_party/quiche/src/http2/hpack/tools/hpack_block_builder.h"

#include "testing/gtest/include/gtest/gtest.h"
#include "net/third_party/quiche/src/http2/platform/api/http2_string_utils.h"

namespace http2 {
namespace test {
namespace {
const bool kUncompressed = false;
const bool kCompressed = true;

// TODO(jamessynge): Once static table code is checked in, switch to using
// constants from there.
const uint32_t kStaticTableMethodGET = 2;
const uint32_t kStaticTablePathSlash = 4;
const uint32_t kStaticTableSchemeHttp = 6;

// Tests of encoding per the RFC. See:
//   http://httpwg.org/specs/rfc7541.html#header.field.representation.examples
// The expected values have been copied from the RFC.
TEST(HpackBlockBuilderTest, ExamplesFromSpecC2) {
  {
    HpackBlockBuilder b;
    b.AppendLiteralNameAndValue(HpackEntryType::kIndexedLiteralHeader,
                                kUncompressed, "custom-key", kUncompressed,
                                "custom-header");
    EXPECT_EQ(26u, b.size());

    const char kExpected[] =
        "\x40"            // == Literal indexed ==
        "\x0a"            // Name length (10)
        "custom-key"      // Name
        "\x0d"            // Value length (13)
        "custom-header";  // Value
    EXPECT_EQ(kExpected, b.buffer());
  }
  {
    HpackBlockBuilder b;
    b.AppendNameIndexAndLiteralValue(HpackEntryType::kUnindexedLiteralHeader, 4,
                                     kUncompressed, "/sample/path");
    EXPECT_EQ(14u, b.size());

    const char kExpected[] =
        "\x04"           // == Literal unindexed, name index 0x04 ==
        "\x0c"           // Value length (12)
        "/sample/path";  // Value
    EXPECT_EQ(kExpected, b.buffer());
  }
  {
    HpackBlockBuilder b;
    b.AppendLiteralNameAndValue(HpackEntryType::kNeverIndexedLiteralHeader,
                                kUncompressed, "password", kUncompressed,
                                "secret");
    EXPECT_EQ(17u, b.size());

    const char kExpected[] =
        "\x10"      // == Literal never indexed ==
        "\x08"      // Name length (8)
        "password"  // Name
        "\x06"      // Value length (6)
        "secret";   // Value
    EXPECT_EQ(kExpected, b.buffer());
  }
  {
    HpackBlockBuilder b;
    b.AppendIndexedHeader(2);
    EXPECT_EQ(1u, b.size());

    const char kExpected[] = "\x82";  // == Indexed (2) ==
    EXPECT_EQ(kExpected, b.buffer());
  }
}

// Tests of encoding per the RFC. See:
//  http://httpwg.org/specs/rfc7541.html#request.examples.without.huffman.coding
TEST(HpackBlockBuilderTest, ExamplesFromSpecC3) {
  {
    // Header block to encode:
    //   :method: GET
    //   :scheme: http
    //   :path: /
    //   :authority: www.example.com
    HpackBlockBuilder b;
    b.AppendIndexedHeader(2);  // :method: GET
    b.AppendIndexedHeader(6);  // :scheme: http
    b.AppendIndexedHeader(4);  // :path: /
    b.AppendNameIndexAndLiteralValue(HpackEntryType::kIndexedLiteralHeader, 1,
                                     kUncompressed, "www.example.com");
    EXPECT_EQ(20u, b.size());

    // Hex dump of encoded data (copied from RFC):
    // 0x0000:  8286 8441 0f77 7777 2e65 7861 6d70 6c65  ...A.www.example
    // 0x0010:  2e63 6f6d                                .com

    const std::string expected =
        Http2HexDecode("828684410f7777772e6578616d706c652e636f6d");
    EXPECT_EQ(expected, b.buffer());
  }
}

// Tests of encoding per the RFC. See:
//   http://httpwg.org/specs/rfc7541.html#request.examples.with.huffman.coding
TEST(HpackBlockBuilderTest, ExamplesFromSpecC4) {
  {
    // Header block to encode:
    //   :method: GET
    //   :scheme: http
    //   :path: /
    //   :authority: www.example.com  (Huffman encoded)
    HpackBlockBuilder b;
    b.AppendIndexedHeader(kStaticTableMethodGET);
    b.AppendIndexedHeader(kStaticTableSchemeHttp);
    b.AppendIndexedHeader(kStaticTablePathSlash);
    const char kHuffmanWwwExampleCom[] = {'\xf1', '\xe3', '\xc2', '\xe5',
                                          '\xf2', '\x3a', '\x6b', '\xa0',
                                          '\xab', '\x90', '\xf4', '\xff'};
    b.AppendNameIndexAndLiteralValue(
        HpackEntryType::kIndexedLiteralHeader, 1, kCompressed,
        quiche::QuicheStringPiece(kHuffmanWwwExampleCom,
                                  sizeof kHuffmanWwwExampleCom));
    EXPECT_EQ(17u, b.size());

    // Hex dump of encoded data (copied from RFC):
    // 0x0000:  8286 8441 8cf1 e3c2 e5f2 3a6b a0ab 90f4  ...A......:k....
    // 0x0010:  ff                                       .

    const std::string expected =
        Http2HexDecode("828684418cf1e3c2e5f23a6ba0ab90f4ff");
    EXPECT_EQ(expected, b.buffer());
  }
}

TEST(HpackBlockBuilderTest, DynamicTableSizeUpdate) {
  {
    HpackBlockBuilder b;
    b.AppendDynamicTableSizeUpdate(0);
    EXPECT_EQ(1u, b.size());

    const char kData[] = {'\x20'};
    quiche::QuicheStringPiece expected(kData, sizeof kData);
    EXPECT_EQ(expected, b.buffer());
  }
  {
    HpackBlockBuilder b;
    b.AppendDynamicTableSizeUpdate(4096);  // The default size.
    EXPECT_EQ(3u, b.size());

    const char kData[] = {'\x3f', '\xe1', '\x1f'};
    quiche::QuicheStringPiece expected(kData, sizeof kData);
    EXPECT_EQ(expected, b.buffer());
  }
  {
    HpackBlockBuilder b;
    b.AppendDynamicTableSizeUpdate(1000000000000);  // A very large value.
    EXPECT_EQ(7u, b.size());

    const char kData[] = {'\x3f', '\xe1', '\x9f', '\x94',
                          '\xa5', '\x8d', '\x1d'};
    quiche::QuicheStringPiece expected(kData, sizeof kData);
    EXPECT_EQ(expected, b.buffer());
  }
}

}  // namespace
}  // namespace test
}  // namespace http2
