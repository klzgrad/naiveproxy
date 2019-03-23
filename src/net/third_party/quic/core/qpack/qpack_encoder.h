// Copyright (c) 2018 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef NET_THIRD_PARTY_QUIC_CORE_QPACK_QPACK_ENCODER_H_
#define NET_THIRD_PARTY_QUIC_CORE_QPACK_QPACK_ENCODER_H_

#include <cstdint>
#include <memory>

#include "net/third_party/quic/core/qpack/qpack_decoder_stream_receiver.h"
#include "net/third_party/quic/core/qpack/qpack_encoder_stream_sender.h"
#include "net/third_party/quic/core/qpack/qpack_header_table.h"
#include "net/third_party/quic/core/quic_types.h"
#include "net/third_party/quic/platform/api/quic_export.h"
#include "net/third_party/quiche/src/spdy/core/hpack/hpack_encoder.h"

namespace spdy {

class SpdyHeaderBlock;

}

namespace quic {

// QPACK encoder class.  Exactly one instance should exist per QUIC connection.
// This class vends a new QpackProgressiveEncoder instance for each new header
// list to be encoded.
class QUIC_EXPORT_PRIVATE QpackEncoder
    : public QpackDecoderStreamReceiver::Delegate {
 public:
  // Interface for receiving notification that an error has occurred on the
  // decoder stream.  This MUST be treated as a connection error of type
  // HTTP_QPACK_DECODER_STREAM_ERROR.
  class QUIC_EXPORT_PRIVATE DecoderStreamErrorDelegate {
   public:
    virtual ~DecoderStreamErrorDelegate() {}

    virtual void OnError(QuicStringPiece error_message) = 0;
  };

  QpackEncoder(
      DecoderStreamErrorDelegate* decoder_stream_error_delegate,
      QpackEncoderStreamSender::Delegate* encoder_stream_sender_delegate);
  ~QpackEncoder() override;

  // This factory method is called to start encoding a header list.
  // |*header_list| must remain valid and must not change
  // during the lifetime of the returned ProgressiveEncoder instance.
  std::unique_ptr<spdy::HpackEncoder::ProgressiveEncoder> EncodeHeaderList(
      QuicStreamId stream_id,
      const spdy::SpdyHeaderBlock* header_list);

  // Decode data received on the decoder stream.
  void DecodeDecoderStreamData(QuicStringPiece data);

  // QpackDecoderStreamReceiver::Delegate implementation
  void OnTableStateSynchronize(uint64_t insert_count) override;
  void OnHeaderAcknowledgement(QuicStreamId stream_id) override;
  void OnStreamCancellation(QuicStreamId stream_id) override;
  void OnErrorDetected(QuicStringPiece error_message) override;

 private:
  DecoderStreamErrorDelegate* const decoder_stream_error_delegate_;
  QpackDecoderStreamReceiver decoder_stream_receiver_;
  QpackEncoderStreamSender encoder_stream_sender_;
  QpackHeaderTable header_table_;
};

}  // namespace quic

#endif  // NET_THIRD_PARTY_QUIC_CORE_QPACK_QPACK_ENCODER_H_
