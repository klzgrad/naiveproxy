// Copyright (c) 2018 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <string>
#include <tuple>

#include "net/third_party/quiche/src/quic/core/qpack/qpack_decoder_test_utils.h"
#include "net/third_party/quiche/src/quic/core/qpack/qpack_encoder_test_utils.h"
#include "net/third_party/quiche/src/quic/core/qpack/qpack_test_utils.h"
#include "net/third_party/quiche/src/quic/core/qpack/qpack_utils.h"
#include "net/third_party/quiche/src/quic/platform/api/quic_string_piece.h"
#include "net/third_party/quiche/src/quic/platform/api/quic_test.h"
#include "net/third_party/quiche/src/spdy/core/spdy_header_block.h"

using ::testing::Combine;
using ::testing::Values;

namespace quic {
namespace test {
namespace {

class QpackRoundTripTest : public QuicTestWithParam<FragmentMode> {
 public:
  QpackRoundTripTest() = default;
  ~QpackRoundTripTest() override = default;

  spdy::SpdyHeaderBlock EncodeThenDecode(
      const spdy::SpdyHeaderBlock& header_list) {
    NoopDecoderStreamErrorDelegate decoder_stream_error_delegate;
    NoopQpackStreamSenderDelegate encoder_stream_sender_delegate;
    QpackEncoder encoder(&decoder_stream_error_delegate);
    encoder.set_qpack_stream_sender_delegate(&encoder_stream_sender_delegate);
    std::string encoded_header_block =
        encoder.EncodeHeaderList(/* stream_id = */ 1, header_list);

    TestHeadersHandler handler;
    NoopEncoderStreamErrorDelegate encoder_stream_error_delegate;
    NoopQpackStreamSenderDelegate decoder_stream_sender_delegate;
    // TODO(b/112770235): Test dynamic table and blocked streams.
    QpackDecode(
        /* maximum_dynamic_table_capacity = */ 0,
        /* maximum_blocked_streams = */ 0, &encoder_stream_error_delegate,
        &decoder_stream_sender_delegate, &handler,
        FragmentModeToFragmentSizeGenerator(GetParam()), encoded_header_block);

    EXPECT_TRUE(handler.decoding_completed());
    EXPECT_FALSE(handler.decoding_error_detected());

    return handler.ReleaseHeaderList();
  }
};

INSTANTIATE_TEST_SUITE_P(,
                         QpackRoundTripTest,
                         Values(FragmentMode::kSingleChunk,
                                FragmentMode::kOctetByOctet));

TEST_P(QpackRoundTripTest, Empty) {
  spdy::SpdyHeaderBlock header_list;
  spdy::SpdyHeaderBlock output = EncodeThenDecode(header_list);
  EXPECT_EQ(header_list, output);
}

TEST_P(QpackRoundTripTest, EmptyName) {
  spdy::SpdyHeaderBlock header_list;
  header_list["foo"] = "bar";
  header_list[""] = "bar";

  spdy::SpdyHeaderBlock output = EncodeThenDecode(header_list);
  EXPECT_EQ(header_list, output);
}

TEST_P(QpackRoundTripTest, EmptyValue) {
  spdy::SpdyHeaderBlock header_list;
  header_list["foo"] = "";
  header_list[""] = "";

  spdy::SpdyHeaderBlock output = EncodeThenDecode(header_list);
  EXPECT_EQ(header_list, output);
}

TEST_P(QpackRoundTripTest, MultipleWithLongEntries) {
  spdy::SpdyHeaderBlock header_list;
  header_list["foo"] = "bar";
  header_list[":path"] = "/";
  header_list["foobaar"] = std::string(127, 'Z');
  header_list[std::string(1000, 'b')] = std::string(1000, 'c');

  spdy::SpdyHeaderBlock output = EncodeThenDecode(header_list);
  EXPECT_EQ(header_list, output);
}

TEST_P(QpackRoundTripTest, StaticTable) {
  {
    spdy::SpdyHeaderBlock header_list;
    header_list[":method"] = "GET";
    header_list["accept-encoding"] = "gzip, deflate";
    header_list["cache-control"] = "";
    header_list["foo"] = "bar";
    header_list[":path"] = "/";

    spdy::SpdyHeaderBlock output = EncodeThenDecode(header_list);
    EXPECT_EQ(header_list, output);
  }
  {
    spdy::SpdyHeaderBlock header_list;
    header_list[":method"] = "POST";
    header_list["accept-encoding"] = "brotli";
    header_list["cache-control"] = "foo";
    header_list["foo"] = "bar";
    header_list[":path"] = "/";

    spdy::SpdyHeaderBlock output = EncodeThenDecode(header_list);
    EXPECT_EQ(header_list, output);
  }
  {
    spdy::SpdyHeaderBlock header_list;
    header_list[":method"] = "CONNECT";
    header_list["accept-encoding"] = "";
    header_list["foo"] = "bar";
    header_list[":path"] = "/";

    spdy::SpdyHeaderBlock output = EncodeThenDecode(header_list);
    EXPECT_EQ(header_list, output);
  }
}

TEST_P(QpackRoundTripTest, ValueHasNullCharacter) {
  spdy::SpdyHeaderBlock header_list;
  header_list["foo"] = QuicStringPiece("bar\0bar\0baz", 11);

  spdy::SpdyHeaderBlock output = EncodeThenDecode(header_list);
  EXPECT_EQ(header_list, output);
}

}  // namespace
}  // namespace test
}  // namespace quic
