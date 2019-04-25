// Copyright (c) 2018 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/third_party/quic/core/qpack/qpack_encoder_test_utils.h"

#include "net/third_party/quiche/src/spdy/core/hpack/hpack_encoder.h"

namespace quic {
namespace test {

void NoopDecoderStreamErrorDelegate::OnDecoderStreamError(
    QuicStringPiece error_message) {}

void NoopEncoderStreamSenderDelegate::WriteEncoderStreamData(
    QuicStringPiece data) {}

QuicString QpackEncode(
    QpackEncoder::DecoderStreamErrorDelegate* decoder_stream_error_delegate,
    QpackEncoderStreamSender::Delegate* encoder_stream_sender_delegate,
    const FragmentSizeGenerator& fragment_size_generator,
    const spdy::SpdyHeaderBlock* header_list) {
  QpackEncoder encoder(decoder_stream_error_delegate,
                       encoder_stream_sender_delegate);
  auto progressive_encoder =
      encoder.EncodeHeaderList(/* stream_id = */ 1, header_list);

  QuicString output;
  while (progressive_encoder->HasNext()) {
    progressive_encoder->Next(fragment_size_generator(), &output);
  }

  return output;
}

}  // namespace test
}  // namespace quic
