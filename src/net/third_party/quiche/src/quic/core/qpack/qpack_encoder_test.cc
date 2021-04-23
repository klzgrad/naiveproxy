// Copyright (c) 2018 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "quic/core/qpack/qpack_encoder.h"

#include <limits>
#include <string>

#include "absl/strings/escaping.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"
#include "quic/platform/api/quic_flags.h"
#include "quic/platform/api/quic_test.h"
#include "quic/test_tools/qpack/qpack_encoder_peer.h"
#include "quic/test_tools/qpack/qpack_encoder_test_utils.h"
#include "quic/test_tools/qpack/qpack_header_table_peer.h"
#include "quic/test_tools/qpack/qpack_test_utils.h"
#include "common/platform/api/quiche_text_utils.h"

using ::testing::_;
using ::testing::Eq;
using ::testing::StrictMock;

namespace quic {
namespace test {
namespace {

class QpackEncoderTest : public QuicTest {
 protected:
  QpackEncoderTest()
      : encoder_(&decoder_stream_error_delegate_),
        encoder_stream_sent_byte_count_(0) {
    encoder_.set_qpack_stream_sender_delegate(&encoder_stream_sender_delegate_);
    encoder_.SetMaximumBlockedStreams(1);
  }

  ~QpackEncoderTest() override = default;

  std::string Encode(const spdy::Http2HeaderBlock& header_list) {
    return encoder_.EncodeHeaderList(/* stream_id = */ 1, header_list,
                                     &encoder_stream_sent_byte_count_);
  }

  StrictMock<MockDecoderStreamErrorDelegate> decoder_stream_error_delegate_;
  StrictMock<MockQpackStreamSenderDelegate> encoder_stream_sender_delegate_;
  QpackEncoder encoder_;
  QuicByteCount encoder_stream_sent_byte_count_;
};

TEST_F(QpackEncoderTest, Empty) {
  spdy::Http2HeaderBlock header_list;
  std::string output = Encode(header_list);

  EXPECT_EQ(absl::HexStringToBytes("0000"), output);
}

TEST_F(QpackEncoderTest, EmptyName) {
  spdy::Http2HeaderBlock header_list;
  header_list[""] = "foo";
  std::string output = Encode(header_list);

  EXPECT_EQ(absl::HexStringToBytes("0000208294e7"), output);
}

TEST_F(QpackEncoderTest, EmptyValue) {
  spdy::Http2HeaderBlock header_list;
  header_list["foo"] = "";
  std::string output = Encode(header_list);

  EXPECT_EQ(absl::HexStringToBytes("00002a94e700"), output);
}

TEST_F(QpackEncoderTest, EmptyNameAndValue) {
  spdy::Http2HeaderBlock header_list;
  header_list[""] = "";
  std::string output = Encode(header_list);

  EXPECT_EQ(absl::HexStringToBytes("00002000"), output);
}

TEST_F(QpackEncoderTest, Simple) {
  spdy::Http2HeaderBlock header_list;
  header_list["foo"] = "bar";
  std::string output = Encode(header_list);

  EXPECT_EQ(absl::HexStringToBytes("00002a94e703626172"), output);
}

TEST_F(QpackEncoderTest, Multiple) {
  spdy::Http2HeaderBlock header_list;
  header_list["foo"] = "bar";
  // 'Z' would be Huffman encoded to 8 bits, so no Huffman encoding is used.
  header_list["ZZZZZZZ"] = std::string(127, 'Z');
  std::string output = Encode(header_list);

  EXPECT_EQ(
      absl::HexStringToBytes(
          "0000"                // prefix
          "2a94e703626172"      // foo: bar
          "27005a5a5a5a5a5a5a"  // 7 octet long header name, the smallest number
                                // that does not fit on a 3-bit prefix.
          "7f005a5a5a5a5a5a5a"  // 127 octet long header value, the smallest
          "5a5a5a5a5a5a5a5a5a"  // number that does not fit on a 7-bit prefix.
          "5a5a5a5a5a5a5a5a5a5a5a5a5a5a5a5a5a5a5a5a5a5a5a5a5a5a5a5a5a5a5a5a5a5a"
          "5a5a5a5a5a5a5a5a5a5a5a5a5a5a5a5a5a5a5a5a5a5a5a5a5a5a5a5a5a5a5a5a5a5a"
          "5a5a5a5a5a5a5a5a5a5a5a5a5a5a5a5a5a5a5a5a5a5a5a5a5a5a5a5a5a5a5a5a5a5a"
          "5a5a5a5a5a5a5a5a5a"),
      output);
}

TEST_F(QpackEncoderTest, StaticTable) {
  {
    spdy::Http2HeaderBlock header_list;
    header_list[":method"] = "GET";
    header_list["accept-encoding"] = "gzip, deflate, br";
    header_list["location"] = "";

    std::string output = Encode(header_list);
    EXPECT_EQ(absl::HexStringToBytes("0000d1dfcc"), output);
  }
  {
    spdy::Http2HeaderBlock header_list;
    header_list[":method"] = "POST";
    header_list["accept-encoding"] = "compress";
    header_list["location"] = "foo";

    std::string output = Encode(header_list);
    EXPECT_EQ(absl::HexStringToBytes("0000d45f108621e9aec2a11f5c8294e7"),
              output);
  }
  {
    spdy::Http2HeaderBlock header_list;
    header_list[":method"] = "TRACE";
    header_list["accept-encoding"] = "";

    std::string output = Encode(header_list);
    EXPECT_EQ(absl::HexStringToBytes("00005f000554524143455f1000"), output);
  }
}

TEST_F(QpackEncoderTest, DecoderStreamError) {
  EXPECT_CALL(decoder_stream_error_delegate_,
              OnDecoderStreamError(QUIC_QPACK_DECODER_STREAM_INTEGER_TOO_LARGE,
                                   Eq("Encoded integer too large.")));

  QpackEncoder encoder(&decoder_stream_error_delegate_);
  encoder.set_qpack_stream_sender_delegate(&encoder_stream_sender_delegate_);
  encoder.decoder_stream_receiver()->Decode(
      absl::HexStringToBytes("ffffffffffffffffffffff"));
}

TEST_F(QpackEncoderTest, SplitAlongNullCharacter) {
  spdy::Http2HeaderBlock header_list;
  header_list["foo"] = absl::string_view("bar\0bar\0baz", 11);
  std::string output = Encode(header_list);

  EXPECT_EQ(absl::HexStringToBytes("0000"            // prefix
                                   "2a94e703626172"  // foo: bar
                                   "2a94e703626172"  // foo: bar
                                   "2a94e70362617a"  // foo: baz
                                   ),
            output);
}

TEST_F(QpackEncoderTest, ZeroInsertCountIncrement) {
  // Encoder receives insert count increment with forbidden value 0.
  EXPECT_CALL(
      decoder_stream_error_delegate_,
      OnDecoderStreamError(QUIC_QPACK_DECODER_STREAM_INVALID_ZERO_INCREMENT,
                           Eq("Invalid increment value 0.")));
  encoder_.OnInsertCountIncrement(0);
}

TEST_F(QpackEncoderTest, TooLargeInsertCountIncrement) {
  // Encoder receives insert count increment with value that increases Known
  // Received Count to a value (one) which is larger than the number of dynamic
  // table insertions sent (zero).
  EXPECT_CALL(
      decoder_stream_error_delegate_,
      OnDecoderStreamError(QUIC_QPACK_DECODER_STREAM_IMPOSSIBLE_INSERT_COUNT,
                           Eq("Increment value 1 raises known received count "
                              "to 1 exceeding inserted entry count 0")));
  encoder_.OnInsertCountIncrement(1);
}

// Regression test for https://crbug.com/1014372.
TEST_F(QpackEncoderTest, InsertCountIncrementOverflow) {
  QpackHeaderTable* header_table = QpackEncoderPeer::header_table(&encoder_);

  // Set dynamic table capacity large enough to hold one entry.
  header_table->SetMaximumDynamicTableCapacity(4096);
  header_table->SetDynamicTableCapacity(4096);
  // Insert one entry into the header table.
  header_table->InsertEntry("foo", "bar");

  // Receive Insert Count Increment instruction with increment value 1.
  encoder_.OnInsertCountIncrement(1);

  // Receive Insert Count Increment instruction that overflows the known
  // received count.  This must result in an error instead of a crash.
  EXPECT_CALL(decoder_stream_error_delegate_,
              OnDecoderStreamError(
                  QUIC_QPACK_DECODER_STREAM_INCREMENT_OVERFLOW,
                  Eq("Insert Count Increment instruction causes overflow.")));
  encoder_.OnInsertCountIncrement(std::numeric_limits<uint64_t>::max());
}

TEST_F(QpackEncoderTest, InvalidHeaderAcknowledgement) {
  // Encoder receives header acknowledgement for a stream on which no header
  // block with dynamic table entries was ever sent.
  EXPECT_CALL(
      decoder_stream_error_delegate_,
      OnDecoderStreamError(QUIC_QPACK_DECODER_STREAM_INCORRECT_ACKNOWLEDGEMENT,
                           Eq("Header Acknowledgement received for stream 0 "
                              "with no outstanding header blocks.")));
  encoder_.OnHeaderAcknowledgement(/* stream_id = */ 0);
}

TEST_F(QpackEncoderTest, DynamicTable) {
  encoder_.SetMaximumBlockedStreams(1);
  encoder_.SetMaximumDynamicTableCapacity(4096);
  encoder_.SetDynamicTableCapacity(4096);

  spdy::Http2HeaderBlock header_list;
  header_list["foo"] = "bar";
  header_list.AppendValueOrAddHeader("foo",
                                     "baz");  // name matches dynamic entry
  header_list["cookie"] = "baz";              // name matches static entry

  // Set Dynamic Table Capacity instruction.
  std::string set_dyanamic_table_capacity = absl::HexStringToBytes("3fe11f");
  // Insert three entries into the dynamic table.
  std::string insert_entries = absl::HexStringToBytes(
      "62"          // insert without name reference
      "94e7"        // Huffman-encoded name "foo"
      "03626172"    // value "bar"
      "80"          // insert with name reference, dynamic index 0
      "0362617a"    // value "baz"
      "c5"          // insert with name reference, static index 5
      "0362617a");  // value "baz"
  EXPECT_CALL(encoder_stream_sender_delegate_,
              WriteStreamData(Eq(
                  absl::StrCat(set_dyanamic_table_capacity, insert_entries))));

  EXPECT_EQ(absl::HexStringToBytes(
                "0400"      // prefix
                "828180"),  // dynamic entries with relative index 0, 1, and 2
            Encode(header_list));

  EXPECT_EQ(insert_entries.size(), encoder_stream_sent_byte_count_);
}

// There is no room in the dynamic table after inserting the first entry.
TEST_F(QpackEncoderTest, SmallDynamicTable) {
  encoder_.SetMaximumBlockedStreams(1);
  encoder_.SetMaximumDynamicTableCapacity(QpackEntry::Size("foo", "bar"));
  encoder_.SetDynamicTableCapacity(QpackEntry::Size("foo", "bar"));

  spdy::Http2HeaderBlock header_list;
  header_list["foo"] = "bar";
  header_list.AppendValueOrAddHeader("foo",
                                     "baz");  // name matches dynamic entry
  header_list["cookie"] = "baz";              // name matches static entry
  header_list["bar"] = "baz";                 // no match

  // Set Dynamic Table Capacity instruction.
  std::string set_dyanamic_table_capacity = absl::HexStringToBytes("3f07");
  // Insert one entry into the dynamic table.
  std::string insert_entry = absl::HexStringToBytes(
      "62"          // insert without name reference
      "94e7"        // Huffman-encoded name "foo"
      "03626172");  // value "bar"
  EXPECT_CALL(encoder_stream_sender_delegate_,
              WriteStreamData(
                  Eq(absl::StrCat(set_dyanamic_table_capacity, insert_entry))));

  EXPECT_EQ(absl::HexStringToBytes("0200"  // prefix
                                   "80"    // dynamic entry 0
                                   "40"    // reference to dynamic entry 0 name
                                   "0362617a"  // with literal value "baz"
                                   "55"  // reference to static entry 5 name
                                   "0362617a"    // with literal value "baz"
                                   "23626172"    // literal name "bar"
                                   "0362617a"),  // with literal value "baz"
            Encode(header_list));

  EXPECT_EQ(insert_entry.size(), encoder_stream_sent_byte_count_);
}

TEST_F(QpackEncoderTest, BlockedStream) {
  encoder_.SetMaximumBlockedStreams(1);
  encoder_.SetMaximumDynamicTableCapacity(4096);
  encoder_.SetDynamicTableCapacity(4096);

  spdy::Http2HeaderBlock header_list1;
  header_list1["foo"] = "bar";

  // Set Dynamic Table Capacity instruction.
  std::string set_dyanamic_table_capacity = absl::HexStringToBytes("3fe11f");
  // Insert one entry into the dynamic table.
  std::string insert_entry1 = absl::HexStringToBytes(
      "62"          // insert without name reference
      "94e7"        // Huffman-encoded name "foo"
      "03626172");  // value "bar"
  EXPECT_CALL(encoder_stream_sender_delegate_,
              WriteStreamData(Eq(
                  absl::StrCat(set_dyanamic_table_capacity, insert_entry1))));

  EXPECT_EQ(absl::HexStringToBytes("0200"  // prefix
                                   "80"),  // dynamic entry 0
            encoder_.EncodeHeaderList(/* stream_id = */ 1, header_list1,
                                      &encoder_stream_sent_byte_count_));
  EXPECT_EQ(insert_entry1.size(), encoder_stream_sent_byte_count_);

  // Stream 1 is blocked.  Stream 2 is not allowed to block.
  spdy::Http2HeaderBlock header_list2;
  header_list2["foo"] = "bar";  // name and value match dynamic entry
  header_list2.AppendValueOrAddHeader("foo",
                                      "baz");  // name matches dynamic entry
  header_list2["cookie"] = "baz";              // name matches static entry
  header_list2["bar"] = "baz";                 // no match

  EXPECT_EQ(absl::HexStringToBytes("0000"        // prefix
                                   "2a94e7"      // literal name "foo"
                                   "03626172"    // with literal value "bar"
                                   "2a94e7"      // literal name "foo"
                                   "0362617a"    // with literal value "baz"
                                   "55"          // name of static entry 5
                                   "0362617a"    // with literal value "baz"
                                   "23626172"    // literal name "bar"
                                   "0362617a"),  // with literal value "baz"
            encoder_.EncodeHeaderList(/* stream_id = */ 2, header_list2,
                                      &encoder_stream_sent_byte_count_));
  EXPECT_EQ(0u, encoder_stream_sent_byte_count_);

  // Peer acknowledges receipt of one dynamic table entry.
  // Stream 1 is no longer blocked.
  encoder_.OnInsertCountIncrement(1);

  // Insert three entries into the dynamic table.
  std::string insert_entries = absl::HexStringToBytes(
      "80"          // insert with name reference, dynamic index 0
      "0362617a"    // value "baz"
      "c5"          // insert with name reference, static index 5
      "0362617a"    // value "baz"
      "43"          // insert without name reference
      "626172"      // name "bar"
      "0362617a");  // value "baz"
  EXPECT_CALL(encoder_stream_sender_delegate_,
              WriteStreamData(Eq(insert_entries)));

  EXPECT_EQ(absl::HexStringToBytes("0500"        // prefix
                                   "83828180"),  // dynamic entries
            encoder_.EncodeHeaderList(/* stream_id = */ 3, header_list2,
                                      &encoder_stream_sent_byte_count_));
  EXPECT_EQ(insert_entries.size(), encoder_stream_sent_byte_count_);

  // Stream 3 is blocked.  Stream 4 is not allowed to block, but it can
  // reference already acknowledged dynamic entry 0.
  EXPECT_EQ(absl::HexStringToBytes("0200"        // prefix
                                   "80"          // dynamic entry 0
                                   "2a94e7"      // literal name "foo"
                                   "0362617a"    // with literal value "baz"
                                   "2c21cfd4c5"  // literal name "cookie"
                                   "0362617a"    // with literal value "baz"
                                   "23626172"    // literal name "bar"
                                   "0362617a"),  // with literal value "baz"
            encoder_.EncodeHeaderList(/* stream_id = */ 4, header_list2,
                                      &encoder_stream_sent_byte_count_));
  EXPECT_EQ(0u, encoder_stream_sent_byte_count_);

  // Peer acknowledges receipt of two more dynamic table entries.
  // Stream 3 is still blocked.
  encoder_.OnInsertCountIncrement(2);

  // Stream 5 is not allowed to block, but it can reference already acknowledged
  // dynamic entries 0, 1, and 2.
  EXPECT_EQ(absl::HexStringToBytes("0400"        // prefix
                                   "828180"      // dynamic entries
                                   "23626172"    // literal name "bar"
                                   "0362617a"),  // with literal value "baz"
            encoder_.EncodeHeaderList(/* stream_id = */ 5, header_list2,
                                      &encoder_stream_sent_byte_count_));
  EXPECT_EQ(0u, encoder_stream_sent_byte_count_);

  // Peer acknowledges decoding header block on stream 3.
  // Stream 3 is not blocked any longer.
  encoder_.OnHeaderAcknowledgement(3);

  EXPECT_EQ(absl::HexStringToBytes("0500"        // prefix
                                   "83828180"),  // dynamic entries
            encoder_.EncodeHeaderList(/* stream_id = */ 6, header_list2,
                                      &encoder_stream_sent_byte_count_));
  EXPECT_EQ(0u, encoder_stream_sent_byte_count_);
}

TEST_F(QpackEncoderTest, Draining) {
  spdy::Http2HeaderBlock header_list1;
  header_list1["one"] = "foo";
  header_list1["two"] = "foo";
  header_list1["three"] = "foo";
  header_list1["four"] = "foo";
  header_list1["five"] = "foo";
  header_list1["six"] = "foo";
  header_list1["seven"] = "foo";
  header_list1["eight"] = "foo";
  header_list1["nine"] = "foo";
  header_list1["ten"] = "foo";

  // Make just enough room in the dynamic table for the header list plus the
  // first entry duplicated.  This will ensure that the oldest entries are
  // draining.
  uint64_t maximum_dynamic_table_capacity = 0;
  for (const auto& header_field : header_list1) {
    maximum_dynamic_table_capacity +=
        QpackEntry::Size(header_field.first, header_field.second);
  }
  maximum_dynamic_table_capacity += QpackEntry::Size("one", "foo");
  encoder_.SetMaximumDynamicTableCapacity(maximum_dynamic_table_capacity);
  encoder_.SetDynamicTableCapacity(maximum_dynamic_table_capacity);

  // Set Dynamic Table Capacity instruction and insert ten entries into the
  // dynamic table.
  EXPECT_CALL(encoder_stream_sender_delegate_, WriteStreamData(_));

  EXPECT_EQ(absl::HexStringToBytes("0b00"                    // prefix
                                   "89888786858483828180"),  // dynamic entries
            Encode(header_list1));

  // Entry is identical to oldest one, which is draining.  It will be
  // duplicated and referenced.
  spdy::Http2HeaderBlock header_list2;
  header_list2["one"] = "foo";

  // Duplicate oldest entry.
  EXPECT_CALL(encoder_stream_sender_delegate_,
              WriteStreamData(Eq(absl::HexStringToBytes("09"))));

  EXPECT_EQ(absl::HexStringToBytes("0c00"  // prefix
                                   "80"),  // most recent dynamic table entry
            Encode(header_list2));

  spdy::Http2HeaderBlock header_list3;
  // Entry is identical to second oldest one, which is draining.  There is no
  // room to duplicate, it will be encoded with string literals.
  header_list3.AppendValueOrAddHeader("two", "foo");
  // Entry has name identical to second oldest one, which is draining.  There is
  // no room to insert new entry, it will be encoded with string literals.
  header_list3.AppendValueOrAddHeader("two", "bar");

  EXPECT_EQ(absl::HexStringToBytes("0000"        // prefix
                                   "2374776f"    // literal name "two"
                                   "8294e7"      // literal value "foo"
                                   "2374776f"    // literal name "two"
                                   "03626172"),  // literal value "bar"
            Encode(header_list3));
}

TEST_F(QpackEncoderTest, DynamicTableCapacityLessThanMaximum) {
  encoder_.SetMaximumDynamicTableCapacity(1024);
  encoder_.SetDynamicTableCapacity(30);

  QpackHeaderTable* header_table = QpackEncoderPeer::header_table(&encoder_);

  EXPECT_EQ(1024u,
            QpackHeaderTablePeer::maximum_dynamic_table_capacity(header_table));
  EXPECT_EQ(30u, QpackHeaderTablePeer::dynamic_table_capacity(header_table));
}

}  // namespace
}  // namespace test
}  // namespace quic
