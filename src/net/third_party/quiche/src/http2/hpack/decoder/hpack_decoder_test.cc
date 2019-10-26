// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/third_party/quiche/src/http2/hpack/decoder/hpack_decoder.h"

// Tests of HpackDecoder.

#include <string>
#include <tuple>
#include <utility>
#include <vector>

#include "testing/gmock/include/gmock/gmock.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "net/third_party/quiche/src/http2/decoder/decode_buffer.h"
#include "net/third_party/quiche/src/http2/hpack/decoder/hpack_decoder_listener.h"
#include "net/third_party/quiche/src/http2/hpack/decoder/hpack_decoder_state.h"
#include "net/third_party/quiche/src/http2/hpack/decoder/hpack_decoder_tables.h"
#include "net/third_party/quiche/src/http2/hpack/hpack_string.h"
#include "net/third_party/quiche/src/http2/hpack/http2_hpack_constants.h"
#include "net/third_party/quiche/src/http2/hpack/tools/hpack_block_builder.h"
#include "net/third_party/quiche/src/http2/hpack/tools/hpack_example.h"
#include "net/third_party/quiche/src/http2/http2_constants.h"
#include "net/third_party/quiche/src/http2/platform/api/http2_logging.h"
#include "net/third_party/quiche/src/http2/platform/api/http2_test_helpers.h"
#include "net/third_party/quiche/src/http2/test_tools/http2_random.h"
#include "net/third_party/quiche/src/http2/tools/random_util.h"

using ::testing::AssertionFailure;
using ::testing::AssertionResult;
using ::testing::AssertionSuccess;
using ::testing::ElementsAreArray;
using ::testing::HasSubstr;

namespace http2 {
namespace test {
class HpackDecoderStatePeer {
 public:
  static HpackDecoderTables* GetDecoderTables(HpackDecoderState* state) {
    return &state->decoder_tables_;
  }
  static void set_listener(HpackDecoderState* state,
                           HpackDecoderListener* listener) {
    state->listener_ = listener;
  }
};
class HpackDecoderPeer {
 public:
  static HpackDecoderState* GetDecoderState(HpackDecoder* decoder) {
    return &decoder->decoder_state_;
  }
  static HpackDecoderTables* GetDecoderTables(HpackDecoder* decoder) {
    return HpackDecoderStatePeer::GetDecoderTables(GetDecoderState(decoder));
  }
};

namespace {

typedef std::tuple<HpackEntryType, std::string, std::string> HpackHeaderEntry;
typedef std::vector<HpackHeaderEntry> HpackHeaderEntries;

// TODO(jamessynge): Create a ...test_utils.h file with the mock listener
// and with VerifyDynamicTableContents.
class MockHpackDecoderListener : public HpackDecoderListener {
 public:
  MOCK_METHOD0(OnHeaderListStart, void());
  MOCK_METHOD3(OnHeader,
               void(HpackEntryType entry_type,
                    const HpackString& name,
                    const HpackString& value));
  MOCK_METHOD0(OnHeaderListEnd, void());
  MOCK_METHOD1(OnHeaderErrorDetected, void(Http2StringPiece error_message));
};

class HpackDecoderTest : public ::testing::TestWithParam<bool>,
                         public HpackDecoderListener {
 protected:
  // Note that we initialize the random number generator with the same seed
  // for each individual test, therefore the order in which the tests are
  // executed does not effect the sequence produced by the RNG within any
  // one test.
  HpackDecoderTest() : decoder_(this, 4096) {
    fragment_the_hpack_block_ = GetParam();
  }
  ~HpackDecoderTest() override = default;

  void OnHeaderListStart() override {
    ASSERT_FALSE(saw_start_);
    ASSERT_FALSE(saw_end_);
    saw_start_ = true;
    header_entries_.clear();
  }

  // Called for each header name-value pair that is decoded, in the order they
  // appear in the HPACK block. Multiple values for a given key will be emitted
  // as multiple calls to OnHeader.
  void OnHeader(HpackEntryType entry_type,
                const HpackString& name,
                const HpackString& value) override {
    ASSERT_TRUE(saw_start_);
    ASSERT_FALSE(saw_end_);
    //     header_entries_.push_back({entry_type, name.ToString(),
    //     value.ToString()});
    header_entries_.emplace_back(entry_type, name.ToString(), value.ToString());
  }

  // OnHeaderBlockEnd is called after successfully decoding an HPACK block. Will
  // only be called once per block, even if it extends into CONTINUATION frames.
  // A callback method which notifies when the parser finishes handling a
  // header block (i.e. the containing frame has the END_STREAM flag set).
  // Also indicates the total number of bytes in this block.
  void OnHeaderListEnd() override {
    ASSERT_TRUE(saw_start_);
    ASSERT_FALSE(saw_end_);
    ASSERT_TRUE(error_messages_.empty());
    saw_end_ = true;
  }

  // OnHeaderErrorDetected is called if an error is detected while decoding.
  // error_message may be used in a GOAWAY frame as the Opaque Data.
  void OnHeaderErrorDetected(Http2StringPiece error_message) override {
    ASSERT_TRUE(saw_start_);
    error_messages_.push_back(std::string(error_message));
    // No further callbacks should be made at this point, so replace 'this' as
    // the listener with mock_listener_, which is a strict mock, so will
    // generate an error for any calls.
    HpackDecoderStatePeer::set_listener(
        HpackDecoderPeer::GetDecoderState(&decoder_), &mock_listener_);
  }

  AssertionResult DecodeBlock(Http2StringPiece block) {
    HTTP2_VLOG(1) << "HpackDecoderTest::DecodeBlock";

    VERIFY_FALSE(decoder_.error_detected());
    VERIFY_TRUE(error_messages_.empty());
    VERIFY_FALSE(saw_start_);
    VERIFY_FALSE(saw_end_);
    header_entries_.clear();

    VERIFY_FALSE(decoder_.error_detected());
    VERIFY_TRUE(decoder_.StartDecodingBlock());
    VERIFY_FALSE(decoder_.error_detected());

    if (fragment_the_hpack_block_) {
      // See note in ctor regarding RNG.
      while (!block.empty()) {
        size_t fragment_size = random_.RandomSizeSkewedLow(block.size());
        DecodeBuffer db(block.substr(0, fragment_size));
        VERIFY_TRUE(decoder_.DecodeFragment(&db));
        VERIFY_EQ(0u, db.Remaining());
        block.remove_prefix(fragment_size);
      }
    } else {
      DecodeBuffer db(block);
      VERIFY_TRUE(decoder_.DecodeFragment(&db));
      VERIFY_EQ(0u, db.Remaining());
    }
    VERIFY_FALSE(decoder_.error_detected());

    VERIFY_TRUE(decoder_.EndDecodingBlock());
    if (saw_end_) {
      VERIFY_FALSE(decoder_.error_detected());
      VERIFY_TRUE(error_messages_.empty());
    } else {
      VERIFY_TRUE(decoder_.error_detected());
      VERIFY_FALSE(error_messages_.empty());
    }

    saw_start_ = saw_end_ = false;
    return AssertionSuccess();
  }

  const HpackDecoderTables& GetDecoderTables() {
    return *HpackDecoderPeer::GetDecoderTables(&decoder_);
  }
  const HpackStringPair* Lookup(size_t index) {
    return GetDecoderTables().Lookup(index);
  }
  size_t current_header_table_size() {
    return GetDecoderTables().current_header_table_size();
  }
  size_t header_table_size_limit() {
    return GetDecoderTables().header_table_size_limit();
  }
  void set_header_table_size_limit(size_t size) {
    HpackDecoderPeer::GetDecoderTables(&decoder_)->DynamicTableSizeUpdate(size);
  }

  // dynamic_index is one-based, because that is the way RFC 7541 shows it.
  AssertionResult VerifyEntry(size_t dynamic_index,
                              const char* name,
                              const char* value) {
    const HpackStringPair* entry =
        Lookup(dynamic_index + kFirstDynamicTableIndex - 1);
    VERIFY_NE(entry, nullptr);
    VERIFY_EQ(entry->name.ToStringPiece(), name);
    VERIFY_EQ(entry->value.ToStringPiece(), value);
    return AssertionSuccess();
  }
  AssertionResult VerifyNoEntry(size_t dynamic_index) {
    const HpackStringPair* entry =
        Lookup(dynamic_index + kFirstDynamicTableIndex - 1);
    VERIFY_EQ(entry, nullptr);
    return AssertionSuccess();
  }
  AssertionResult VerifyDynamicTableContents(
      const std::vector<std::pair<const char*, const char*>>& entries) {
    size_t index = 1;
    for (const auto& entry : entries) {
      VERIFY_SUCCESS(VerifyEntry(index, entry.first, entry.second));
      ++index;
    }
    VERIFY_SUCCESS(VerifyNoEntry(index));
    return AssertionSuccess();
  }

  Http2Random random_;
  HpackDecoder decoder_;
  testing::StrictMock<MockHpackDecoderListener> mock_listener_;
  HpackHeaderEntries header_entries_;
  std::vector<std::string> error_messages_;
  bool fragment_the_hpack_block_;
  bool saw_start_ = false;
  bool saw_end_ = false;
};
INSTANTIATE_TEST_SUITE_P(AllWays, HpackDecoderTest, ::testing::Bool());

// Test based on RFC 7541, section C.3: Request Examples without Huffman Coding.
// This section shows several consecutive header lists, corresponding to HTTP
// requests, on the same connection.
// http://httpwg.org/specs/rfc7541.html#rfc.section.C.3
TEST_P(HpackDecoderTest, C3_RequestExamples) {
  // C.3.1 First Request
  std::string hpack_block = HpackExampleToStringOrDie(R"(
      82                                      | == Indexed - Add ==
                                              |   idx = 2
                                              | -> :method: GET
      86                                      | == Indexed - Add ==
                                              |   idx = 6
                                              | -> :scheme: http
      84                                      | == Indexed - Add ==
                                              |   idx = 4
                                              | -> :path: /
      41                                      | == Literal indexed ==
                                              |   Indexed name (idx = 1)
                                              |     :authority
      0f                                      |   Literal value (len = 15)
      7777 772e 6578 616d 706c 652e 636f 6d   | www.example.com
                                              | -> :authority:
                                              |   www.example.com
  )");
  EXPECT_TRUE(DecodeBlock(hpack_block));
  ASSERT_THAT(
      header_entries_,
      ElementsAreArray({
          HpackHeaderEntry{HpackEntryType::kIndexedHeader, ":method", "GET"},
          HpackHeaderEntry{HpackEntryType::kIndexedHeader, ":scheme", "http"},
          HpackHeaderEntry{HpackEntryType::kIndexedHeader, ":path", "/"},
          HpackHeaderEntry{HpackEntryType::kIndexedLiteralHeader, ":authority",
                           "www.example.com"},
      }));

  // Dynamic Table (after decoding):
  //
  //   [  1] (s =  57) :authority: www.example.com
  //         Table size:  57
  ASSERT_TRUE(VerifyDynamicTableContents({{":authority", "www.example.com"}}));
  ASSERT_EQ(57u, current_header_table_size());

  // C.3.2 Second Request
  hpack_block = HpackExampleToStringOrDie(R"(
      82                                      | == Indexed - Add ==
                                              |   idx = 2
                                              | -> :method: GET
      86                                      | == Indexed - Add ==
                                              |   idx = 6
                                              | -> :scheme: http
      84                                      | == Indexed - Add ==
                                              |   idx = 4
                                              | -> :path: /
      be                                      | == Indexed - Add ==
                                              |   idx = 62
                                              | -> :authority:
                                              |   www.example.com
      58                                      | == Literal indexed ==
                                              |   Indexed name (idx = 24)
                                              |     cache-control
      08                                      |   Literal value (len = 8)
      6e6f 2d63 6163 6865                     | no-cache
                                              | -> cache-control: no-cache
  )");
  EXPECT_TRUE(DecodeBlock(hpack_block));
  ASSERT_THAT(
      header_entries_,
      ElementsAreArray({
          HpackHeaderEntry{HpackEntryType::kIndexedHeader, ":method", "GET"},
          HpackHeaderEntry{HpackEntryType::kIndexedHeader, ":scheme", "http"},
          HpackHeaderEntry{HpackEntryType::kIndexedHeader, ":path", "/"},
          HpackHeaderEntry{HpackEntryType::kIndexedHeader, ":authority",
                           "www.example.com"},
          HpackHeaderEntry{HpackEntryType::kIndexedLiteralHeader,
                           "cache-control", "no-cache"},
      }));

  // Dynamic Table (after decoding):
  //
  //   [  1] (s =  53) cache-control: no-cache
  //   [  2] (s =  57) :authority: www.example.com
  //         Table size: 110
  ASSERT_TRUE(VerifyDynamicTableContents(
      {{"cache-control", "no-cache"}, {":authority", "www.example.com"}}));
  ASSERT_EQ(110u, current_header_table_size());

  // C.3.2 Third Request
  hpack_block = HpackExampleToStringOrDie(R"(
      82                                      | == Indexed - Add ==
                                              |   idx = 2
                                              | -> :method: GET
      87                                      | == Indexed - Add ==
                                              |   idx = 7
                                              | -> :scheme: https
      85                                      | == Indexed - Add ==
                                              |   idx = 5
                                              | -> :path: /index.html
      bf                                      | == Indexed - Add ==
                                              |   idx = 63
                                              | -> :authority:
                                              |   www.example.com
      40                                      | == Literal indexed ==
      0a                                      |   Literal name (len = 10)
      6375 7374 6f6d 2d6b 6579                | custom-key
      0c                                      |   Literal value (len = 12)
      6375 7374 6f6d 2d76 616c 7565           | custom-value
                                              | -> custom-key:
                                              |   custom-value
  )");
  EXPECT_TRUE(DecodeBlock(hpack_block));
  ASSERT_THAT(
      header_entries_,
      ElementsAreArray({
          HpackHeaderEntry{HpackEntryType::kIndexedHeader, ":method", "GET"},
          HpackHeaderEntry{HpackEntryType::kIndexedHeader, ":scheme", "https"},
          HpackHeaderEntry{HpackEntryType::kIndexedHeader, ":path",
                           "/index.html"},
          HpackHeaderEntry{HpackEntryType::kIndexedHeader, ":authority",
                           "www.example.com"},
          HpackHeaderEntry{HpackEntryType::kIndexedLiteralHeader, "custom-key",
                           "custom-value"},
      }));

  // Dynamic Table (after decoding):
  //
  //   [  1] (s =  54) custom-key: custom-value
  //   [  2] (s =  53) cache-control: no-cache
  //   [  3] (s =  57) :authority: www.example.com
  //         Table size: 164
  ASSERT_TRUE(VerifyDynamicTableContents({{"custom-key", "custom-value"},
                                          {"cache-control", "no-cache"},
                                          {":authority", "www.example.com"}}));
  ASSERT_EQ(164u, current_header_table_size());
}

// Test based on RFC 7541, section C.4 Request Examples with Huffman Coding.
// This section shows the same examples as the previous section but uses
// Huffman encoding for the literal values.
// http://httpwg.org/specs/rfc7541.html#rfc.section.C.4
TEST_P(HpackDecoderTest, C4_RequestExamplesWithHuffmanEncoding) {
  // C.4.1 First Request
  std::string hpack_block = HpackExampleToStringOrDie(R"(
      82                                      | == Indexed - Add ==
                                              |   idx = 2
                                              | -> :method: GET
      86                                      | == Indexed - Add ==
                                              |   idx = 6
                                              | -> :scheme: http
      84                                      | == Indexed - Add ==
                                              |   idx = 4
                                              | -> :path: /
      41                                      | == Literal indexed ==
                                              |   Indexed name (idx = 1)
                                              |     :authority
      8c                                      |   Literal value (len = 12)
                                              |     Huffman encoded:
      f1e3 c2e5 f23a 6ba0 ab90 f4ff           | .....:k.....
                                              |     Decoded:
                                              | www.example.com
                                              | -> :authority:
                                              |   www.example.com
  )");
  EXPECT_TRUE(DecodeBlock(hpack_block));
  ASSERT_THAT(
      header_entries_,
      ElementsAreArray({
          HpackHeaderEntry{HpackEntryType::kIndexedHeader, ":method", "GET"},
          HpackHeaderEntry{HpackEntryType::kIndexedHeader, ":scheme", "http"},
          HpackHeaderEntry{HpackEntryType::kIndexedHeader, ":path", "/"},
          HpackHeaderEntry{HpackEntryType::kIndexedLiteralHeader, ":authority",
                           "www.example.com"},
      }));

  // Dynamic Table (after decoding):
  //
  //   [  1] (s =  57) :authority: www.example.com
  //         Table size:  57
  ASSERT_TRUE(VerifyDynamicTableContents({{":authority", "www.example.com"}}));
  ASSERT_EQ(57u, current_header_table_size());

  // C.4.2 Second Request
  hpack_block = HpackExampleToStringOrDie(R"(
      82                                      | == Indexed - Add ==
                                              |   idx = 2
                                              | -> :method: GET
      86                                      | == Indexed - Add ==
                                              |   idx = 6
                                              | -> :scheme: http
      84                                      | == Indexed - Add ==
                                              |   idx = 4
                                              | -> :path: /
      be                                      | == Indexed - Add ==
                                              |   idx = 62
                                              | -> :authority:
                                              |   www.example.com
      58                                      | == Literal indexed ==
                                              |   Indexed name (idx = 24)
                                              |     cache-control
      86                                      |   Literal value (len = 6)
                                              |     Huffman encoded:
      a8eb 1064 9cbf                          | ...d..
                                              |     Decoded:
                                              | no-cache
                                              | -> cache-control: no-cache
  )");
  EXPECT_TRUE(DecodeBlock(hpack_block));
  ASSERT_THAT(
      header_entries_,
      ElementsAreArray({
          HpackHeaderEntry{HpackEntryType::kIndexedHeader, ":method", "GET"},
          HpackHeaderEntry{HpackEntryType::kIndexedHeader, ":scheme", "http"},
          HpackHeaderEntry{HpackEntryType::kIndexedHeader, ":path", "/"},
          HpackHeaderEntry{HpackEntryType::kIndexedHeader, ":authority",
                           "www.example.com"},
          HpackHeaderEntry{HpackEntryType::kIndexedLiteralHeader,
                           "cache-control", "no-cache"},
      }));

  // Dynamic Table (after decoding):
  //
  //   [  1] (s =  53) cache-control: no-cache
  //   [  2] (s =  57) :authority: www.example.com
  //         Table size: 110
  ASSERT_TRUE(VerifyDynamicTableContents(
      {{"cache-control", "no-cache"}, {":authority", "www.example.com"}}));
  ASSERT_EQ(110u, current_header_table_size());

  // C.4.2 Third Request
  hpack_block = HpackExampleToStringOrDie(R"(
    82                                      | == Indexed - Add ==
                                            |   idx = 2
                                            | -> :method: GET
    87                                      | == Indexed - Add ==
                                            |   idx = 7
                                            | -> :scheme: https
    85                                      | == Indexed - Add ==
                                            |   idx = 5
                                            | -> :path: /index.html
    bf                                      | == Indexed - Add ==
                                            |   idx = 63
                                            | -> :authority:
                                            |   www.example.com
    40                                      | == Literal indexed ==
    88                                      |   Literal name (len = 8)
                                            |     Huffman encoded:
    25a8 49e9 5ba9 7d7f                     | %.I.[.}.
                                            |     Decoded:
                                            | custom-key
    89                                      |   Literal value (len = 9)
                                            |     Huffman encoded:
    25a8 49e9 5bb8 e8b4 bf                  | %.I.[....
                                            |     Decoded:
                                            | custom-value
                                            | -> custom-key:
                                            |   custom-value
  )");
  EXPECT_TRUE(DecodeBlock(hpack_block));
  ASSERT_THAT(
      header_entries_,
      ElementsAreArray({
          HpackHeaderEntry{HpackEntryType::kIndexedHeader, ":method", "GET"},
          HpackHeaderEntry{HpackEntryType::kIndexedHeader, ":scheme", "https"},
          HpackHeaderEntry{HpackEntryType::kIndexedHeader, ":path",
                           "/index.html"},
          HpackHeaderEntry{HpackEntryType::kIndexedHeader, ":authority",
                           "www.example.com"},
          HpackHeaderEntry{HpackEntryType::kIndexedLiteralHeader, "custom-key",
                           "custom-value"},
      }));

  // Dynamic Table (after decoding):
  //
  //   [  1] (s =  54) custom-key: custom-value
  //   [  2] (s =  53) cache-control: no-cache
  //   [  3] (s =  57) :authority: www.example.com
  //         Table size: 164
  ASSERT_TRUE(VerifyDynamicTableContents({{"custom-key", "custom-value"},
                                          {"cache-control", "no-cache"},
                                          {":authority", "www.example.com"}}));
  ASSERT_EQ(164u, current_header_table_size());
}

// Test based on RFC 7541, section C.5: Response Examples without Huffman
// Coding. This section shows several consecutive header lists, corresponding
// to HTTP responses, on the same connection. The HTTP/2 setting parameter
// SETTINGS_HEADER_TABLE_SIZE is set to the value of 256 octets, causing
// some evictions to occur.
// http://httpwg.org/specs/rfc7541.html#rfc.section.C.5
TEST_P(HpackDecoderTest, C5_ResponseExamples) {
  set_header_table_size_limit(256);

  // C.5.1 First Response
  //
  // Header list to encode:
  //
  //   :status: 302
  //   cache-control: private
  //   date: Mon, 21 Oct 2013 20:13:21 GMT
  //   location: https://www.example.com

  std::string hpack_block = HpackExampleToStringOrDie(R"(
      48                                      | == Literal indexed ==
                                              |   Indexed name (idx = 8)
                                              |     :status
      03                                      |   Literal value (len = 3)
      3330 32                                 | 302
                                              | -> :status: 302
      58                                      | == Literal indexed ==
                                              |   Indexed name (idx = 24)
                                              |     cache-control
      07                                      |   Literal value (len = 7)
      7072 6976 6174 65                       | private
                                              | -> cache-control: private
      61                                      | == Literal indexed ==
                                              |   Indexed name (idx = 33)
                                              |     date
      1d                                      |   Literal value (len = 29)
      4d6f 6e2c 2032 3120 4f63 7420 3230 3133 | Mon, 21 Oct 2013
      2032 303a 3133 3a32 3120 474d 54        |  20:13:21 GMT
                                              | -> date: Mon, 21 Oct 2013
                                              |   20:13:21 GMT
      6e                                      | == Literal indexed ==
                                              |   Indexed name (idx = 46)
                                              |     location
      17                                      |   Literal value (len = 23)
      6874 7470 733a 2f2f 7777 772e 6578 616d | https://www.exam
      706c 652e 636f 6d                       | ple.com
                                              | -> location:
                                              |   https://www.example.com
  )");
  EXPECT_TRUE(DecodeBlock(hpack_block));
  ASSERT_THAT(header_entries_,
              ElementsAreArray({
                  HpackHeaderEntry{HpackEntryType::kIndexedLiteralHeader,
                                   ":status", "302"},
                  HpackHeaderEntry{HpackEntryType::kIndexedLiteralHeader,
                                   "cache-control", "private"},
                  HpackHeaderEntry{HpackEntryType::kIndexedLiteralHeader,
                                   "date", "Mon, 21 Oct 2013 20:13:21 GMT"},
                  HpackHeaderEntry{HpackEntryType::kIndexedLiteralHeader,
                                   "location", "https://www.example.com"},
              }));

  // Dynamic Table (after decoding):
  //
  //   [  1] (s =  63) location: https://www.example.com
  //   [  2] (s =  65) date: Mon, 21 Oct 2013 20:13:21 GMT
  //   [  3] (s =  52) cache-control: private
  //   [  4] (s =  42) :status: 302
  //         Table size: 222
  ASSERT_TRUE(
      VerifyDynamicTableContents({{"location", "https://www.example.com"},
                                  {"date", "Mon, 21 Oct 2013 20:13:21 GMT"},
                                  {"cache-control", "private"},
                                  {":status", "302"}}));
  ASSERT_EQ(222u, current_header_table_size());

  // C.5.2 Second Response
  //
  // The (":status", "302") header field is evicted from the dynamic table to
  // free space to allow adding the (":status", "307") header field.
  //
  // Header list to encode:
  //
  //   :status: 307
  //   cache-control: private
  //   date: Mon, 21 Oct 2013 20:13:21 GMT
  //   location: https://www.example.com

  hpack_block = HpackExampleToStringOrDie(R"(
      48                                      | == Literal indexed ==
                                              |   Indexed name (idx = 8)
                                              |     :status
      03                                      |   Literal value (len = 3)
      3330 37                                 | 307
                                              | - evict: :status: 302
                                              | -> :status: 307
      c1                                      | == Indexed - Add ==
                                              |   idx = 65
                                              | -> cache-control: private
      c0                                      | == Indexed - Add ==
                                              |   idx = 64
                                              | -> date: Mon, 21 Oct 2013
                                              |   20:13:21 GMT
      bf                                      | == Indexed - Add ==
                                              |   idx = 63
                                              | -> location:
                                              |   https://www.example.com
  )");
  EXPECT_TRUE(DecodeBlock(hpack_block));
  ASSERT_THAT(header_entries_,
              ElementsAreArray({
                  HpackHeaderEntry{HpackEntryType::kIndexedLiteralHeader,
                                   ":status", "307"},
                  HpackHeaderEntry{HpackEntryType::kIndexedHeader,
                                   "cache-control", "private"},
                  HpackHeaderEntry{HpackEntryType::kIndexedHeader, "date",
                                   "Mon, 21 Oct 2013 20:13:21 GMT"},
                  HpackHeaderEntry{HpackEntryType::kIndexedHeader, "location",
                                   "https://www.example.com"},
              }));

  // Dynamic Table (after decoding):
  //
  //   [  1] (s =  42) :status: 307
  //   [  2] (s =  63) location: https://www.example.com
  //   [  3] (s =  65) date: Mon, 21 Oct 2013 20:13:21 GMT
  //   [  4] (s =  52) cache-control: private
  //         Table size: 222

  ASSERT_TRUE(
      VerifyDynamicTableContents({{":status", "307"},
                                  {"location", "https://www.example.com"},
                                  {"date", "Mon, 21 Oct 2013 20:13:21 GMT"},
                                  {"cache-control", "private"}}));
  ASSERT_EQ(222u, current_header_table_size());

  // C.5.3 Third Response
  //
  // Several header fields are evicted from the dynamic table during the
  // processing of this header list.
  //
  // Header list to encode:
  //
  //   :status: 200
  //   cache-control: private
  //   date: Mon, 21 Oct 2013 20:13:22 GMT
  //   location: https://www.example.com
  //   content-encoding: gzip
  //   set-cookie: foo=ASDJKHQKBZXOQWEOPIUAXQWEOIU; max-age=3600; version=1
  hpack_block = HpackExampleToStringOrDie(R"(
      88                                      | == Indexed - Add ==
                                              |   idx = 8
                                              | -> :status: 200
      c1                                      | == Indexed - Add ==
                                              |   idx = 65
                                              | -> cache-control: private
      61                                      | == Literal indexed ==
                                              |   Indexed name (idx = 33)
                                              |     date
      1d                                      |   Literal value (len = 29)
      4d6f 6e2c 2032 3120 4f63 7420 3230 3133 | Mon, 21 Oct 2013
      2032 303a 3133 3a32 3220 474d 54        |  20:13:22 GMT
                                              | - evict: cache-control:
                                              |   private
                                              | -> date: Mon, 21 Oct 2013
                                              |   20:13:22 GMT
      c0                                      | == Indexed - Add ==
                                              |   idx = 64
                                              | -> location:
                                              |   https://www.example.com
      5a                                      | == Literal indexed ==
                                              |   Indexed name (idx = 26)
                                              |     content-encoding
      04                                      |   Literal value (len = 4)
      677a 6970                               | gzip
                                              | - evict: date: Mon, 21 Oct
                                              |    2013 20:13:21 GMT
                                              | -> content-encoding: gzip
      77                                      | == Literal indexed ==
                                              |   Indexed name (idx = 55)
                                              |     set-cookie
      38                                      |   Literal value (len = 56)
      666f 6f3d 4153 444a 4b48 514b 425a 584f | foo=ASDJKHQKBZXO
      5157 454f 5049 5541 5851 5745 4f49 553b | QWEOPIUAXQWEOIU;
      206d 6178 2d61 6765 3d33 3630 303b 2076 |  max-age=3600; v
      6572 7369 6f6e 3d31                     | ersion=1
                                              | - evict: location:
                                              |   https://www.example.com
                                              | - evict: :status: 307
                                              | -> set-cookie: foo=ASDJKHQ
                                              |   KBZXOQWEOPIUAXQWEOIU; ma
                                              |   x-age=3600; version=1
  )");
  EXPECT_TRUE(DecodeBlock(hpack_block));
  ASSERT_THAT(
      header_entries_,
      ElementsAreArray({
          HpackHeaderEntry{HpackEntryType::kIndexedHeader, ":status", "200"},
          HpackHeaderEntry{HpackEntryType::kIndexedHeader, "cache-control",
                           "private"},
          HpackHeaderEntry{HpackEntryType::kIndexedLiteralHeader, "date",
                           "Mon, 21 Oct 2013 20:13:22 GMT"},
          HpackHeaderEntry{HpackEntryType::kIndexedHeader, "location",
                           "https://www.example.com"},
          HpackHeaderEntry{HpackEntryType::kIndexedLiteralHeader,
                           "content-encoding", "gzip"},
          HpackHeaderEntry{
              HpackEntryType::kIndexedLiteralHeader, "set-cookie",
              "foo=ASDJKHQKBZXOQWEOPIUAXQWEOIU; max-age=3600; version=1"},
      }));

  // Dynamic Table (after decoding):
  //
  //   [  1] (s =  98) set-cookie: foo=ASDJKHQKBZXOQWEOPIUAXQWEOIU;
  //                    max-age=3600; version=1
  //   [  2] (s =  52) content-encoding: gzip
  //   [  3] (s =  65) date: Mon, 21 Oct 2013 20:13:22 GMT
  //         Table size: 215
  ASSERT_TRUE(VerifyDynamicTableContents(
      {{"set-cookie",
        "foo=ASDJKHQKBZXOQWEOPIUAXQWEOIU; max-age=3600; version=1"},
       {"content-encoding", "gzip"},
       {"date", "Mon, 21 Oct 2013 20:13:22 GMT"}}));
  ASSERT_EQ(215u, current_header_table_size());
}

// Test based on RFC 7541, section C.6: Response Examples with Huffman Coding.
// This section shows the same examples as the previous section but uses Huffman
// encoding for the literal values. The HTTP/2 setting parameter
// SETTINGS_HEADER_TABLE_SIZE is set to the value of 256 octets, causing some
// evictions to occur. The eviction mechanism uses the length of the decoded
// literal values, so the same evictions occur as in the previous section.
// http://httpwg.org/specs/rfc7541.html#rfc.section.C.6
TEST_P(HpackDecoderTest, C6_ResponseExamplesWithHuffmanEncoding) {
  set_header_table_size_limit(256);

  // C.5.1 First Response
  //
  // Header list to encode:
  //
  //   :status: 302
  //   cache-control: private
  //   date: Mon, 21 Oct 2013 20:13:21 GMT
  //   location: https://www.example.com
  std::string hpack_block = HpackExampleToStringOrDie(R"(
      48                                      | == Literal indexed ==
                                              |   Indexed name (idx = 8)
                                              |     :status
      03                                      |   Literal value (len = 3)
      3330 32                                 | 302
                                              | -> :status: 302
      58                                      | == Literal indexed ==
                                              |   Indexed name (idx = 24)
                                              |     cache-control
      07                                      |   Literal value (len = 7)
      7072 6976 6174 65                       | private
                                              | -> cache-control: private
      61                                      | == Literal indexed ==
                                              |   Indexed name (idx = 33)
                                              |     date
      1d                                      |   Literal value (len = 29)
      4d6f 6e2c 2032 3120 4f63 7420 3230 3133 | Mon, 21 Oct 2013
      2032 303a 3133 3a32 3120 474d 54        |  20:13:21 GMT
                                              | -> date: Mon, 21 Oct 2013
                                              |   20:13:21 GMT
      6e                                      | == Literal indexed ==
                                              |   Indexed name (idx = 46)
                                              |     location
      17                                      |   Literal value (len = 23)
      6874 7470 733a 2f2f 7777 772e 6578 616d | https://www.exam
      706c 652e 636f 6d                       | ple.com
                                              | -> location:
                                              |   https://www.example.com
  )");
  EXPECT_TRUE(DecodeBlock(hpack_block));
  ASSERT_THAT(header_entries_,
              ElementsAreArray({
                  HpackHeaderEntry{HpackEntryType::kIndexedLiteralHeader,
                                   ":status", "302"},
                  HpackHeaderEntry{HpackEntryType::kIndexedLiteralHeader,
                                   "cache-control", "private"},
                  HpackHeaderEntry{HpackEntryType::kIndexedLiteralHeader,
                                   "date", "Mon, 21 Oct 2013 20:13:21 GMT"},
                  HpackHeaderEntry{HpackEntryType::kIndexedLiteralHeader,
                                   "location", "https://www.example.com"},
              }));

  // Dynamic Table (after decoding):
  //
  //   [  1] (s =  63) location: https://www.example.com
  //   [  2] (s =  65) date: Mon, 21 Oct 2013 20:13:21 GMT
  //   [  3] (s =  52) cache-control: private
  //   [  4] (s =  42) :status: 302
  //         Table size: 222
  ASSERT_TRUE(
      VerifyDynamicTableContents({{"location", "https://www.example.com"},
                                  {"date", "Mon, 21 Oct 2013 20:13:21 GMT"},
                                  {"cache-control", "private"},
                                  {":status", "302"}}));
  ASSERT_EQ(222u, current_header_table_size());

  // C.5.2 Second Response
  //
  // The (":status", "302") header field is evicted from the dynamic table to
  // free space to allow adding the (":status", "307") header field.
  //
  // Header list to encode:
  //
  //   :status: 307
  //   cache-control: private
  //   date: Mon, 21 Oct 2013 20:13:21 GMT
  //   location: https://www.example.com
  hpack_block = HpackExampleToStringOrDie(R"(
      48                                      | == Literal indexed ==
                                              |   Indexed name (idx = 8)
                                              |     :status
      03                                      |   Literal value (len = 3)
      3330 37                                 | 307
                                              | - evict: :status: 302
                                              | -> :status: 307
      c1                                      | == Indexed - Add ==
                                              |   idx = 65
                                              | -> cache-control: private
      c0                                      | == Indexed - Add ==
                                              |   idx = 64
                                              | -> date: Mon, 21 Oct 2013
                                              |   20:13:21 GMT
      bf                                      | == Indexed - Add ==
                                              |   idx = 63
                                              | -> location:
                                              |   https://www.example.com
  )");
  EXPECT_TRUE(DecodeBlock(hpack_block));
  ASSERT_THAT(header_entries_,
              ElementsAreArray({
                  HpackHeaderEntry{HpackEntryType::kIndexedLiteralHeader,
                                   ":status", "307"},
                  HpackHeaderEntry{HpackEntryType::kIndexedHeader,
                                   "cache-control", "private"},
                  HpackHeaderEntry{HpackEntryType::kIndexedHeader, "date",
                                   "Mon, 21 Oct 2013 20:13:21 GMT"},
                  HpackHeaderEntry{HpackEntryType::kIndexedHeader, "location",
                                   "https://www.example.com"},
              }));

  // Dynamic Table (after decoding):
  //
  //   [  1] (s =  42) :status: 307
  //   [  2] (s =  63) location: https://www.example.com
  //   [  3] (s =  65) date: Mon, 21 Oct 2013 20:13:21 GMT
  //   [  4] (s =  52) cache-control: private
  //         Table size: 222
  ASSERT_TRUE(
      VerifyDynamicTableContents({{":status", "307"},
                                  {"location", "https://www.example.com"},
                                  {"date", "Mon, 21 Oct 2013 20:13:21 GMT"},
                                  {"cache-control", "private"}}));
  ASSERT_EQ(222u, current_header_table_size());

  // C.5.3 Third Response
  //
  // Several header fields are evicted from the dynamic table during the
  // processing of this header list.
  //
  // Header list to encode:
  //
  //   :status: 200
  //   cache-control: private
  //   date: Mon, 21 Oct 2013 20:13:22 GMT
  //   location: https://www.example.com
  //   content-encoding: gzip
  //   set-cookie: foo=ASDJKHQKBZXOQWEOPIUAXQWEOIU; max-age=3600; version=1
  hpack_block = HpackExampleToStringOrDie(R"(
      88                                      | == Indexed - Add ==
                                              |   idx = 8
                                              | -> :status: 200
      c1                                      | == Indexed - Add ==
                                              |   idx = 65
                                              | -> cache-control: private
      61                                      | == Literal indexed ==
                                              |   Indexed name (idx = 33)
                                              |     date
      1d                                      |   Literal value (len = 29)
      4d6f 6e2c 2032 3120 4f63 7420 3230 3133 | Mon, 21 Oct 2013
      2032 303a 3133 3a32 3220 474d 54        |  20:13:22 GMT
                                              | - evict: cache-control:
                                              |   private
                                              | -> date: Mon, 21 Oct 2013
                                              |   20:13:22 GMT
      c0                                      | == Indexed - Add ==
                                              |   idx = 64
                                              | -> location:
                                              |   https://www.example.com
      5a                                      | == Literal indexed ==
                                              |   Indexed name (idx = 26)
                                              |     content-encoding
      04                                      |   Literal value (len = 4)
      677a 6970                               | gzip
                                              | - evict: date: Mon, 21 Oct
                                              |    2013 20:13:21 GMT
                                              | -> content-encoding: gzip
      77                                      | == Literal indexed ==
                                              |   Indexed name (idx = 55)
                                              |     set-cookie
      38                                      |   Literal value (len = 56)
      666f 6f3d 4153 444a 4b48 514b 425a 584f | foo=ASDJKHQKBZXO
      5157 454f 5049 5541 5851 5745 4f49 553b | QWEOPIUAXQWEOIU;
      206d 6178 2d61 6765 3d33 3630 303b 2076 |  max-age=3600; v
      6572 7369 6f6e 3d31                     | ersion=1
                                              | - evict: location:
                                              |   https://www.example.com
                                              | - evict: :status: 307
                                              | -> set-cookie: foo=ASDJKHQ
                                              |   KBZXOQWEOPIUAXQWEOIU; ma
                                              |   x-age=3600; version=1
  )");
  EXPECT_TRUE(DecodeBlock(hpack_block));
  ASSERT_THAT(
      header_entries_,
      ElementsAreArray({
          HpackHeaderEntry{HpackEntryType::kIndexedHeader, ":status", "200"},
          HpackHeaderEntry{HpackEntryType::kIndexedHeader, "cache-control",
                           "private"},
          HpackHeaderEntry{HpackEntryType::kIndexedLiteralHeader, "date",
                           "Mon, 21 Oct 2013 20:13:22 GMT"},
          HpackHeaderEntry{HpackEntryType::kIndexedHeader, "location",
                           "https://www.example.com"},
          HpackHeaderEntry{HpackEntryType::kIndexedLiteralHeader,
                           "content-encoding", "gzip"},
          HpackHeaderEntry{
              HpackEntryType::kIndexedLiteralHeader, "set-cookie",
              "foo=ASDJKHQKBZXOQWEOPIUAXQWEOIU; max-age=3600; version=1"},
      }));

  // Dynamic Table (after decoding):
  //
  //   [  1] (s =  98) set-cookie: foo=ASDJKHQKBZXOQWEOPIUAXQWEOIU;
  //                    max-age=3600; version=1
  //   [  2] (s =  52) content-encoding: gzip
  //   [  3] (s =  65) date: Mon, 21 Oct 2013 20:13:22 GMT
  //         Table size: 215
  ASSERT_TRUE(VerifyDynamicTableContents(
      {{"set-cookie",
        "foo=ASDJKHQKBZXOQWEOPIUAXQWEOIU; max-age=3600; version=1"},
       {"content-encoding", "gzip"},
       {"date", "Mon, 21 Oct 2013 20:13:22 GMT"}}));
  ASSERT_EQ(215u, current_header_table_size());
}

// Confirm that the table size can be changed, but at most twice.
TEST_P(HpackDecoderTest, ProcessesOptionalTableSizeUpdates) {
  EXPECT_EQ(Http2SettingsInfo::DefaultHeaderTableSize(),
            header_table_size_limit());
  // One update allowed.
  {
    HpackBlockBuilder hbb;
    hbb.AppendDynamicTableSizeUpdate(3000);
    EXPECT_TRUE(DecodeBlock(hbb.buffer()));
    EXPECT_EQ(3000u, header_table_size_limit());
    EXPECT_EQ(0u, current_header_table_size());
    EXPECT_TRUE(header_entries_.empty());
  }
  // Two updates allowed.
  {
    HpackBlockBuilder hbb;
    hbb.AppendDynamicTableSizeUpdate(2000);
    hbb.AppendDynamicTableSizeUpdate(2500);
    EXPECT_TRUE(DecodeBlock(hbb.buffer()));
    EXPECT_EQ(2500u, header_table_size_limit());
    EXPECT_EQ(0u, current_header_table_size());
    EXPECT_TRUE(header_entries_.empty());
  }
  // A third update in the same HPACK block is rejected, so the final
  // size is 1000, not 500.
  {
    HpackBlockBuilder hbb;
    hbb.AppendDynamicTableSizeUpdate(1500);
    hbb.AppendDynamicTableSizeUpdate(1000);
    hbb.AppendDynamicTableSizeUpdate(500);
    EXPECT_FALSE(DecodeBlock(hbb.buffer()));
    EXPECT_EQ(1u, error_messages_.size());
    EXPECT_THAT(error_messages_[0], HasSubstr("size update not allowed"));
    EXPECT_EQ(1000u, header_table_size_limit());
    EXPECT_EQ(0u, current_header_table_size());
    EXPECT_TRUE(header_entries_.empty());
  }
  // An error has been detected, so calls to HpackDecoder::DecodeFragment
  // should return immediately.
  DecodeBuffer db("\x80");
  EXPECT_FALSE(decoder_.DecodeFragment(&db));
  EXPECT_EQ(0u, db.Offset());
  EXPECT_EQ(1u, error_messages_.size());
}

// Confirm that the table size can be changed when required, but at most twice.
TEST_P(HpackDecoderTest, ProcessesRequiredTableSizeUpdate) {
  // One update required, two allowed, one provided, followed by a header.
  decoder_.ApplyHeaderTableSizeSetting(1024);
  decoder_.ApplyHeaderTableSizeSetting(2048);
  EXPECT_EQ(Http2SettingsInfo::DefaultHeaderTableSize(),
            header_table_size_limit());
  {
    HpackBlockBuilder hbb;
    hbb.AppendDynamicTableSizeUpdate(1024);
    hbb.AppendIndexedHeader(4);  // :path: /
    EXPECT_TRUE(DecodeBlock(hbb.buffer()));
    EXPECT_THAT(header_entries_,
                ElementsAreArray({HpackHeaderEntry{
                    HpackEntryType::kIndexedHeader, ":path", "/"}}));
    EXPECT_EQ(1024u, header_table_size_limit());
    EXPECT_EQ(0u, current_header_table_size());
  }
  // One update required, two allowed, two provided, followed by a header.
  decoder_.ApplyHeaderTableSizeSetting(1000);
  decoder_.ApplyHeaderTableSizeSetting(1500);
  {
    HpackBlockBuilder hbb;
    hbb.AppendDynamicTableSizeUpdate(500);
    hbb.AppendDynamicTableSizeUpdate(1250);
    hbb.AppendIndexedHeader(5);  // :path: /index.html
    EXPECT_TRUE(DecodeBlock(hbb.buffer()));
    EXPECT_THAT(header_entries_,
                ElementsAreArray({HpackHeaderEntry{
                    HpackEntryType::kIndexedHeader, ":path", "/index.html"}}));
    EXPECT_EQ(1250u, header_table_size_limit());
    EXPECT_EQ(0u, current_header_table_size());
  }
  // One update required, two allowed, three provided, followed by a header.
  // The third update is rejected, so the final size is 1000, not 500.
  decoder_.ApplyHeaderTableSizeSetting(500);
  decoder_.ApplyHeaderTableSizeSetting(1000);
  {
    HpackBlockBuilder hbb;
    hbb.AppendDynamicTableSizeUpdate(200);
    hbb.AppendDynamicTableSizeUpdate(700);
    hbb.AppendDynamicTableSizeUpdate(900);
    hbb.AppendIndexedHeader(5);  // Not decoded.
    EXPECT_FALSE(DecodeBlock(hbb.buffer()));
    EXPECT_FALSE(saw_end_);
    EXPECT_EQ(1u, error_messages_.size());
    EXPECT_THAT(error_messages_[0], HasSubstr("size update not allowed"));
    EXPECT_EQ(700u, header_table_size_limit());
    EXPECT_EQ(0u, current_header_table_size());
    EXPECT_TRUE(header_entries_.empty());
  }
  // Now that an error has been detected, StartDecodingBlock should return
  // false.
  EXPECT_FALSE(decoder_.StartDecodingBlock());
}

// Confirm that required size updates are validated.
TEST_P(HpackDecoderTest, InvalidRequiredSizeUpdate) {
  // Require a size update, but provide one that isn't small enough (must be
  // zero or one, in this case).
  decoder_.ApplyHeaderTableSizeSetting(1);
  decoder_.ApplyHeaderTableSizeSetting(1024);
  HpackBlockBuilder hbb;
  hbb.AppendDynamicTableSizeUpdate(2);
  EXPECT_TRUE(decoder_.StartDecodingBlock());
  DecodeBuffer db(hbb.buffer());
  EXPECT_FALSE(decoder_.DecodeFragment(&db));
  EXPECT_FALSE(saw_end_);
  EXPECT_EQ(1u, error_messages_.size());
  EXPECT_THAT(error_messages_[0], HasSubstr("above low water mark"));
  EXPECT_EQ(Http2SettingsInfo::DefaultHeaderTableSize(),
            header_table_size_limit());
}

// Confirm that required size updates are indeed required before the end.
TEST_P(HpackDecoderTest, RequiredTableSizeChangeBeforeEnd) {
  decoder_.ApplyHeaderTableSizeSetting(1024);
  EXPECT_FALSE(DecodeBlock(""));
  EXPECT_EQ(1u, error_messages_.size());
  EXPECT_THAT(error_messages_[0],
              HasSubstr("Missing dynamic table size update"));
  EXPECT_FALSE(saw_end_);
}

// Confirm that required size updates are indeed required before an
// indexed header.
TEST_P(HpackDecoderTest, RequiredTableSizeChangeBeforeIndexedHeader) {
  decoder_.ApplyHeaderTableSizeSetting(1024);
  HpackBlockBuilder hbb;
  hbb.AppendIndexedHeader(1);
  EXPECT_FALSE(DecodeBlock(hbb.buffer()));
  EXPECT_EQ(1u, error_messages_.size());
  EXPECT_THAT(error_messages_[0],
              HasSubstr("Missing dynamic table size update"));
  EXPECT_FALSE(saw_end_);
  EXPECT_TRUE(header_entries_.empty());
}

// Confirm that required size updates are indeed required before an indexed
// header name.
// TODO(jamessynge): Move some of these to hpack_decoder_state_test.cc.
TEST_P(HpackDecoderTest, RequiredTableSizeChangeBeforeIndexedHeaderName) {
  decoder_.ApplyHeaderTableSizeSetting(1024);
  HpackBlockBuilder hbb;
  hbb.AppendNameIndexAndLiteralValue(HpackEntryType::kIndexedLiteralHeader, 2,
                                     false, "PUT");
  EXPECT_FALSE(DecodeBlock(hbb.buffer()));
  EXPECT_EQ(1u, error_messages_.size());
  EXPECT_THAT(error_messages_[0],
              HasSubstr("Missing dynamic table size update"));
  EXPECT_FALSE(saw_end_);
  EXPECT_TRUE(header_entries_.empty());
}

// Confirm that required size updates are indeed required before a literal
// header name.
TEST_P(HpackDecoderTest, RequiredTableSizeChangeBeforeLiteralName) {
  decoder_.ApplyHeaderTableSizeSetting(1024);
  HpackBlockBuilder hbb;
  hbb.AppendLiteralNameAndValue(HpackEntryType::kNeverIndexedLiteralHeader,
                                false, "name", false, "some data.");
  EXPECT_FALSE(DecodeBlock(hbb.buffer()));
  EXPECT_EQ(1u, error_messages_.size());
  EXPECT_THAT(error_messages_[0],
              HasSubstr("Missing dynamic table size update"));
  EXPECT_FALSE(saw_end_);
  EXPECT_TRUE(header_entries_.empty());
}

// Confirm that an excessively long varint is detected, in this case an
// index of 127, but with lots of additional high-order 0 bits provided,
// too many to be allowed.
TEST_P(HpackDecoderTest, InvalidIndexedHeaderVarint) {
  EXPECT_TRUE(decoder_.StartDecodingBlock());
  DecodeBuffer db("\xff\x80\x80\x80\x80\x80\x80\x80\x80\x80\x80\x00");
  EXPECT_FALSE(decoder_.DecodeFragment(&db));
  EXPECT_TRUE(decoder_.error_detected());
  EXPECT_FALSE(saw_end_);
  EXPECT_EQ(1u, error_messages_.size());
  EXPECT_THAT(error_messages_[0], HasSubstr("malformed"));
  EXPECT_TRUE(header_entries_.empty());
  // Now that an error has been detected, EndDecodingBlock should not succeed.
  EXPECT_FALSE(decoder_.EndDecodingBlock());
}

// Confirm that an invalid index into the tables is detected, in this case an
// index of 0.
TEST_P(HpackDecoderTest, InvalidIndex) {
  EXPECT_TRUE(decoder_.StartDecodingBlock());
  DecodeBuffer db("\x80");
  EXPECT_FALSE(decoder_.DecodeFragment(&db));
  EXPECT_TRUE(decoder_.error_detected());
  EXPECT_FALSE(saw_end_);
  EXPECT_EQ(1u, error_messages_.size());
  EXPECT_THAT(error_messages_[0], HasSubstr("Invalid index"));
  EXPECT_TRUE(header_entries_.empty());
  // Now that an error has been detected, EndDecodingBlock should not succeed.
  EXPECT_FALSE(decoder_.EndDecodingBlock());
}

// Confirm that EndDecodingBlock detects a truncated HPACK block.
TEST_P(HpackDecoderTest, TruncatedBlock) {
  HpackBlockBuilder hbb;
  hbb.AppendDynamicTableSizeUpdate(3000);
  EXPECT_EQ(3u, hbb.size());
  hbb.AppendDynamicTableSizeUpdate(4000);
  EXPECT_EQ(6u, hbb.size());
  // Decodes this block if the whole thing is provided.
  EXPECT_TRUE(DecodeBlock(hbb.buffer()));
  EXPECT_EQ(4000u, header_table_size_limit());
  // Multiple times even.
  EXPECT_TRUE(DecodeBlock(hbb.buffer()));
  EXPECT_EQ(4000u, header_table_size_limit());
  // But not if the block is truncated.
  EXPECT_FALSE(DecodeBlock(hbb.buffer().substr(0, hbb.size() - 1)));
  EXPECT_FALSE(saw_end_);
  EXPECT_EQ(1u, error_messages_.size());
  EXPECT_THAT(error_messages_[0], HasSubstr("truncated"));
  // The first update was decoded.
  EXPECT_EQ(3000u, header_table_size_limit());
  EXPECT_EQ(0u, current_header_table_size());
  EXPECT_TRUE(header_entries_.empty());
}

// Confirm that an oversized string is detected, ending decoding.
TEST_P(HpackDecoderTest, OversizeStringDetected) {
  HpackBlockBuilder hbb;
  hbb.AppendLiteralNameAndValue(HpackEntryType::kNeverIndexedLiteralHeader,
                                false, "name", false, "some data.");
  hbb.AppendLiteralNameAndValue(HpackEntryType::kUnindexedLiteralHeader, false,
                                "name2", false, "longer data");

  // Normally able to decode this block.
  EXPECT_TRUE(DecodeBlock(hbb.buffer()));
  EXPECT_THAT(header_entries_,
              ElementsAreArray(
                  {HpackHeaderEntry{HpackEntryType::kNeverIndexedLiteralHeader,
                                    "name", "some data."},
                   HpackHeaderEntry{HpackEntryType::kUnindexedLiteralHeader,
                                    "name2", "longer data"}}));

  // But not if the maximum size of strings is less than the longest string.
  decoder_.set_max_string_size_bytes(10);
  EXPECT_FALSE(DecodeBlock(hbb.buffer()));
  EXPECT_THAT(
      header_entries_,
      ElementsAreArray({HpackHeaderEntry{
          HpackEntryType::kNeverIndexedLiteralHeader, "name", "some data."}}));
  EXPECT_FALSE(saw_end_);
  EXPECT_EQ(1u, error_messages_.size());
  EXPECT_THAT(error_messages_[0], HasSubstr("too long"));
}

}  // namespace
}  // namespace test
}  // namespace http2
