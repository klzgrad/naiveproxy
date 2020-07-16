// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/third_party/quiche/src/http2/hpack/decoder/hpack_decoder_state.h"

// Tests of HpackDecoderState.

#include <utility>
#include <vector>

#include "testing/gmock/include/gmock/gmock.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "net/third_party/quiche/src/http2/hpack/hpack_string.h"
#include "net/third_party/quiche/src/http2/hpack/http2_hpack_constants.h"
#include "net/third_party/quiche/src/http2/http2_constants.h"
#include "net/third_party/quiche/src/http2/platform/api/http2_logging.h"
#include "net/third_party/quiche/src/http2/platform/api/http2_test_helpers.h"
#include "net/third_party/quiche/src/common/platform/api/quiche_string_piece.h"

using ::testing::AssertionResult;
using ::testing::AssertionSuccess;
using ::testing::Eq;
using ::testing::Mock;
using ::testing::StrictMock;

namespace http2 {
namespace test {
class HpackDecoderStatePeer {
 public:
  static HpackDecoderTables* GetDecoderTables(HpackDecoderState* state) {
    return &state->decoder_tables_;
  }
};

namespace {

class MockHpackDecoderListener : public HpackDecoderListener {
 public:
  MOCK_METHOD0(OnHeaderListStart, void());
  MOCK_METHOD2(OnHeader,
               void(const HpackString& name, const HpackString& value));
  MOCK_METHOD0(OnHeaderListEnd, void());
  MOCK_METHOD1(OnHeaderErrorDetected,
               void(quiche::QuicheStringPiece error_message));
};

enum StringBacking { STATIC, UNBUFFERED, BUFFERED };

class HpackDecoderStateTest : public ::testing::Test {
 protected:
  HpackDecoderStateTest() : decoder_state_(&listener_) {}

  HpackDecoderTables* GetDecoderTables() {
    return HpackDecoderStatePeer::GetDecoderTables(&decoder_state_);
  }

  const HpackStringPair* Lookup(size_t index) {
    return GetDecoderTables()->Lookup(index);
  }

  size_t current_header_table_size() {
    return GetDecoderTables()->current_header_table_size();
  }

  size_t header_table_size_limit() {
    return GetDecoderTables()->header_table_size_limit();
  }

  void set_header_table_size_limit(size_t size) {
    GetDecoderTables()->DynamicTableSizeUpdate(size);
  }

  void SetStringBuffer(const char* s,
                       StringBacking backing,
                       HpackDecoderStringBuffer* string_buffer) {
    switch (backing) {
      case STATIC:
        string_buffer->Set(s, true);
        break;
      case UNBUFFERED:
        string_buffer->Set(s, false);
        break;
      case BUFFERED:
        string_buffer->Set(s, false);
        string_buffer->BufferStringIfUnbuffered();
        break;
    }
  }

  void SetName(const char* s, StringBacking backing) {
    SetStringBuffer(s, backing, &name_buffer_);
  }

  void SetValue(const char* s, StringBacking backing) {
    SetStringBuffer(s, backing, &value_buffer_);
  }

  void SendStartAndVerifyCallback() {
    EXPECT_CALL(listener_, OnHeaderListStart());
    decoder_state_.OnHeaderBlockStart();
    Mock::VerifyAndClearExpectations(&listener_);
  }

  void SendSizeUpdate(size_t size) {
    decoder_state_.OnDynamicTableSizeUpdate(size);
    Mock::VerifyAndClearExpectations(&listener_);
  }

  void SendIndexAndVerifyCallback(size_t index,
                                  HpackEntryType expected_type,
                                  const char* expected_name,
                                  const char* expected_value) {
    EXPECT_CALL(listener_, OnHeader(Eq(expected_name), Eq(expected_value)));
    decoder_state_.OnIndexedHeader(index);
    Mock::VerifyAndClearExpectations(&listener_);
  }

  void SendValueAndVerifyCallback(size_t name_index,
                                  HpackEntryType entry_type,
                                  const char* name,
                                  const char* value,
                                  StringBacking value_backing) {
    SetValue(value, value_backing);
    EXPECT_CALL(listener_, OnHeader(Eq(name), Eq(value)));
    decoder_state_.OnNameIndexAndLiteralValue(entry_type, name_index,
                                              &value_buffer_);
    Mock::VerifyAndClearExpectations(&listener_);
  }

  void SendNameAndValueAndVerifyCallback(HpackEntryType entry_type,
                                         const char* name,
                                         StringBacking name_backing,
                                         const char* value,
                                         StringBacking value_backing) {
    SetName(name, name_backing);
    SetValue(value, value_backing);
    EXPECT_CALL(listener_, OnHeader(Eq(name), Eq(value)));
    decoder_state_.OnLiteralNameAndValue(entry_type, &name_buffer_,
                                         &value_buffer_);
    Mock::VerifyAndClearExpectations(&listener_);
  }

  void SendEndAndVerifyCallback() {
    EXPECT_CALL(listener_, OnHeaderListEnd());
    decoder_state_.OnHeaderBlockEnd();
    Mock::VerifyAndClearExpectations(&listener_);
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

  StrictMock<MockHpackDecoderListener> listener_;
  HpackDecoderState decoder_state_;
  HpackDecoderStringBuffer name_buffer_, value_buffer_;
};

// Test based on RFC 7541, section C.3: Request Examples without Huffman Coding.
// This section shows several consecutive header lists, corresponding to HTTP
// requests, on the same connection.
TEST_F(HpackDecoderStateTest, C3_RequestExamples) {
  // C.3.1 First Request
  //
  // Header list to encode:
  //
  //   :method: GET
  //   :scheme: http
  //   :path: /
  //   :authority: www.example.com

  SendStartAndVerifyCallback();
  SendIndexAndVerifyCallback(2, HpackEntryType::kIndexedHeader, ":method",
                             "GET");
  SendIndexAndVerifyCallback(6, HpackEntryType::kIndexedHeader, ":scheme",
                             "http");
  SendIndexAndVerifyCallback(4, HpackEntryType::kIndexedHeader, ":path", "/");
  SendValueAndVerifyCallback(1, HpackEntryType::kIndexedLiteralHeader,
                             ":authority", "www.example.com", UNBUFFERED);
  SendEndAndVerifyCallback();

  // Dynamic Table (after decoding):
  //
  //   [  1] (s =  57) :authority: www.example.com
  //         Table size:  57

  ASSERT_TRUE(VerifyDynamicTableContents({{":authority", "www.example.com"}}));
  ASSERT_EQ(57u, current_header_table_size());

  // C.3.2 Second Request
  //
  // Header list to encode:
  //
  //   :method: GET
  //   :scheme: http
  //   :path: /
  //   :authority: www.example.com
  //   cache-control: no-cache

  SendStartAndVerifyCallback();
  SendIndexAndVerifyCallback(2, HpackEntryType::kIndexedHeader, ":method",
                             "GET");
  SendIndexAndVerifyCallback(6, HpackEntryType::kIndexedHeader, ":scheme",
                             "http");
  SendIndexAndVerifyCallback(4, HpackEntryType::kIndexedHeader, ":path", "/");
  SendIndexAndVerifyCallback(62, HpackEntryType::kIndexedHeader, ":authority",
                             "www.example.com");
  SendValueAndVerifyCallback(24, HpackEntryType::kIndexedLiteralHeader,
                             "cache-control", "no-cache", UNBUFFERED);
  SendEndAndVerifyCallback();

  // Dynamic Table (after decoding):
  //
  //   [  1] (s =  53) cache-control: no-cache
  //   [  2] (s =  57) :authority: www.example.com
  //         Table size: 110

  ASSERT_TRUE(VerifyDynamicTableContents(
      {{"cache-control", "no-cache"}, {":authority", "www.example.com"}}));
  ASSERT_EQ(110u, current_header_table_size());

  // C.3.3 Third Request
  //
  // Header list to encode:
  //
  //   :method: GET
  //   :scheme: https
  //   :path: /index.html
  //   :authority: www.example.com
  //   custom-key: custom-value

  SendStartAndVerifyCallback();
  SendIndexAndVerifyCallback(2, HpackEntryType::kIndexedHeader, ":method",
                             "GET");
  SendIndexAndVerifyCallback(7, HpackEntryType::kIndexedHeader, ":scheme",
                             "https");
  SendIndexAndVerifyCallback(5, HpackEntryType::kIndexedHeader, ":path",
                             "/index.html");
  SendIndexAndVerifyCallback(63, HpackEntryType::kIndexedHeader, ":authority",
                             "www.example.com");
  SendNameAndValueAndVerifyCallback(HpackEntryType::kIndexedLiteralHeader,
                                    "custom-key", UNBUFFERED, "custom-value",
                                    UNBUFFERED);
  SendEndAndVerifyCallback();

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
TEST_F(HpackDecoderStateTest, C5_ResponseExamples) {
  set_header_table_size_limit(256);

  // C.5.1 First Response
  //
  // Header list to encode:
  //
  //   :status: 302
  //   cache-control: private
  //   date: Mon, 21 Oct 2013 20:13:21 GMT
  //   location: https://www.example.com

  SendStartAndVerifyCallback();
  SendValueAndVerifyCallback(8, HpackEntryType::kIndexedLiteralHeader,
                             ":status", "302", BUFFERED);
  SendValueAndVerifyCallback(24, HpackEntryType::kIndexedLiteralHeader,
                             "cache-control", "private", UNBUFFERED);
  SendValueAndVerifyCallback(33, HpackEntryType::kIndexedLiteralHeader, "date",
                             "Mon, 21 Oct 2013 20:13:21 GMT", UNBUFFERED);
  SendValueAndVerifyCallback(46, HpackEntryType::kIndexedLiteralHeader,
                             "location", "https://www.example.com", UNBUFFERED);
  SendEndAndVerifyCallback();

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

  SendStartAndVerifyCallback();
  SendValueAndVerifyCallback(8, HpackEntryType::kIndexedLiteralHeader,
                             ":status", "307", BUFFERED);
  SendIndexAndVerifyCallback(65, HpackEntryType::kIndexedHeader,
                             "cache-control", "private");
  SendIndexAndVerifyCallback(64, HpackEntryType::kIndexedHeader, "date",
                             "Mon, 21 Oct 2013 20:13:21 GMT");
  SendIndexAndVerifyCallback(63, HpackEntryType::kIndexedHeader, "location",
                             "https://www.example.com");
  SendEndAndVerifyCallback();

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

  SendStartAndVerifyCallback();
  SendIndexAndVerifyCallback(8, HpackEntryType::kIndexedHeader, ":status",
                             "200");
  SendIndexAndVerifyCallback(65, HpackEntryType::kIndexedHeader,
                             "cache-control", "private");
  SendValueAndVerifyCallback(33, HpackEntryType::kIndexedLiteralHeader, "date",
                             "Mon, 21 Oct 2013 20:13:22 GMT", BUFFERED);
  SendIndexAndVerifyCallback(64, HpackEntryType::kIndexedHeader, "location",
                             "https://www.example.com");
  SendValueAndVerifyCallback(26, HpackEntryType::kIndexedLiteralHeader,
                             "content-encoding", "gzip", UNBUFFERED);
  SendValueAndVerifyCallback(
      55, HpackEntryType::kIndexedLiteralHeader, "set-cookie",
      "foo=ASDJKHQKBZXOQWEOPIUAXQWEOIU; max-age=3600; version=1", BUFFERED);
  SendEndAndVerifyCallback();

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
TEST_F(HpackDecoderStateTest, OptionalTableSizeChanges) {
  SendStartAndVerifyCallback();
  EXPECT_EQ(Http2SettingsInfo::DefaultHeaderTableSize(),
            header_table_size_limit());
  SendSizeUpdate(1024);
  EXPECT_EQ(1024u, header_table_size_limit());
  SendSizeUpdate(0);
  EXPECT_EQ(0u, header_table_size_limit());

  // Three updates aren't allowed.
  EXPECT_CALL(listener_, OnHeaderErrorDetected(
                             Eq("Dynamic table size update not allowed")));
  SendSizeUpdate(0);
}

// Confirm that required size updates are indeed required before headers.
TEST_F(HpackDecoderStateTest, RequiredTableSizeChangeBeforeHeader) {
  decoder_state_.ApplyHeaderTableSizeSetting(1024);
  decoder_state_.ApplyHeaderTableSizeSetting(2048);

  // First provide the required update, and an allowed second update.
  SendStartAndVerifyCallback();
  EXPECT_EQ(Http2SettingsInfo::DefaultHeaderTableSize(),
            header_table_size_limit());
  SendSizeUpdate(1024);
  EXPECT_EQ(1024u, header_table_size_limit());
  SendSizeUpdate(1500);
  EXPECT_EQ(1500u, header_table_size_limit());
  SendEndAndVerifyCallback();

  // Another HPACK block, but this time missing the required size update.
  decoder_state_.ApplyHeaderTableSizeSetting(1024);
  SendStartAndVerifyCallback();
  EXPECT_CALL(listener_,
              OnHeaderErrorDetected(Eq("Missing dynamic table size update")));
  decoder_state_.OnIndexedHeader(1);

  // Further decoded entries are ignored.
  decoder_state_.OnIndexedHeader(1);
  decoder_state_.OnDynamicTableSizeUpdate(1);
  SetValue("value", UNBUFFERED);
  decoder_state_.OnNameIndexAndLiteralValue(
      HpackEntryType::kIndexedLiteralHeader, 4, &value_buffer_);
  SetName("name", UNBUFFERED);
  decoder_state_.OnLiteralNameAndValue(HpackEntryType::kIndexedLiteralHeader,
                                       &name_buffer_, &value_buffer_);
  decoder_state_.OnHeaderBlockEnd();
  decoder_state_.OnHpackDecodeError(HpackDecodingError::kIndexVarintError);
}

// Confirm that required size updates are validated.
TEST_F(HpackDecoderStateTest, InvalidRequiredSizeUpdate) {
  // Require a size update, but provide one that isn't small enough.
  decoder_state_.ApplyHeaderTableSizeSetting(1024);
  SendStartAndVerifyCallback();
  EXPECT_EQ(Http2SettingsInfo::DefaultHeaderTableSize(),
            header_table_size_limit());
  EXPECT_CALL(
      listener_,
      OnHeaderErrorDetected(
          Eq("Initial dynamic table size update is above low water mark")));
  SendSizeUpdate(2048);
}

// Confirm that required size updates are indeed required before the end.
TEST_F(HpackDecoderStateTest, RequiredTableSizeChangeBeforeEnd) {
  decoder_state_.ApplyHeaderTableSizeSetting(1024);
  SendStartAndVerifyCallback();
  EXPECT_CALL(listener_,
              OnHeaderErrorDetected(Eq("Missing dynamic table size update")));
  decoder_state_.OnHeaderBlockEnd();
}

// Confirm that optional size updates are validated.
TEST_F(HpackDecoderStateTest, InvalidOptionalSizeUpdate) {
  // Require a size update, but provide one that isn't small enough.
  SendStartAndVerifyCallback();
  EXPECT_EQ(Http2SettingsInfo::DefaultHeaderTableSize(),
            header_table_size_limit());
  EXPECT_CALL(listener_,
              OnHeaderErrorDetected(Eq(
                  "Dynamic table size update is above acknowledged setting")));
  SendSizeUpdate(Http2SettingsInfo::DefaultHeaderTableSize() + 1);
}

TEST_F(HpackDecoderStateTest, InvalidStaticIndex) {
  SendStartAndVerifyCallback();
  EXPECT_CALL(listener_,
              OnHeaderErrorDetected(
                  Eq("Invalid index in indexed header field representation")));
  decoder_state_.OnIndexedHeader(0);
}

TEST_F(HpackDecoderStateTest, InvalidDynamicIndex) {
  SendStartAndVerifyCallback();
  EXPECT_CALL(listener_,
              OnHeaderErrorDetected(
                  Eq("Invalid index in indexed header field representation")));
  decoder_state_.OnIndexedHeader(kFirstDynamicTableIndex);
}

TEST_F(HpackDecoderStateTest, InvalidNameIndex) {
  SendStartAndVerifyCallback();
  EXPECT_CALL(listener_,
              OnHeaderErrorDetected(Eq("Invalid index in literal header field "
                                       "with indexed name representation")));
  SetValue("value", UNBUFFERED);
  decoder_state_.OnNameIndexAndLiteralValue(
      HpackEntryType::kIndexedLiteralHeader, kFirstDynamicTableIndex,
      &value_buffer_);
}

TEST_F(HpackDecoderStateTest, ErrorsSuppressCallbacks) {
  SendStartAndVerifyCallback();
  EXPECT_CALL(listener_,
              OnHeaderErrorDetected(Eq("Name Huffman encoding error")));
  decoder_state_.OnHpackDecodeError(HpackDecodingError::kNameHuffmanError);

  // Further decoded entries are ignored.
  decoder_state_.OnIndexedHeader(1);
  decoder_state_.OnDynamicTableSizeUpdate(1);
  SetValue("value", UNBUFFERED);
  decoder_state_.OnNameIndexAndLiteralValue(
      HpackEntryType::kIndexedLiteralHeader, 4, &value_buffer_);
  SetName("name", UNBUFFERED);
  decoder_state_.OnLiteralNameAndValue(HpackEntryType::kIndexedLiteralHeader,
                                       &name_buffer_, &value_buffer_);
  decoder_state_.OnHeaderBlockEnd();
  decoder_state_.OnHpackDecodeError(HpackDecodingError::kIndexVarintError);
}

}  // namespace
}  // namespace test
}  // namespace http2
