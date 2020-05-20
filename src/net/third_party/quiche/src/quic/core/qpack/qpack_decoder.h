// Copyright (c) 2018 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef QUICHE_QUIC_CORE_QPACK_QPACK_DECODER_H_
#define QUICHE_QUIC_CORE_QPACK_QPACK_DECODER_H_

#include <cstdint>
#include <memory>
#include <set>

#include "net/third_party/quiche/src/quic/core/qpack/qpack_decoder_stream_sender.h"
#include "net/third_party/quiche/src/quic/core/qpack/qpack_encoder_stream_receiver.h"
#include "net/third_party/quiche/src/quic/core/qpack/qpack_header_table.h"
#include "net/third_party/quiche/src/quic/core/qpack/qpack_progressive_decoder.h"
#include "net/third_party/quiche/src/quic/core/quic_types.h"
#include "net/third_party/quiche/src/quic/platform/api/quic_export.h"
#include "net/third_party/quiche/src/common/platform/api/quiche_string_piece.h"

namespace quic {

// QPACK decoder class.  Exactly one instance should exist per QUIC connection.
// This class vends a new QpackProgressiveDecoder instance for each new header
// list to be encoded.
class QUIC_EXPORT_PRIVATE QpackDecoder
    : public QpackEncoderStreamReceiver::Delegate,
      public QpackProgressiveDecoder::BlockedStreamLimitEnforcer,
      public QpackProgressiveDecoder::DecodingCompletedVisitor {
 public:
  // Interface for receiving notification that an error has occurred on the
  // encoder stream.  This MUST be treated as a connection error of type
  // HTTP_QPACK_ENCODER_STREAM_ERROR.
  class QUIC_EXPORT_PRIVATE EncoderStreamErrorDelegate {
   public:
    virtual ~EncoderStreamErrorDelegate() {}

    virtual void OnEncoderStreamError(
        quiche::QuicheStringPiece error_message) = 0;
  };

  QpackDecoder(uint64_t maximum_dynamic_table_capacity,
               uint64_t maximum_blocked_streams,
               EncoderStreamErrorDelegate* encoder_stream_error_delegate);
  ~QpackDecoder() override;

  // Signal to the peer's encoder that a stream is reset.  This lets the peer's
  // encoder know that no more header blocks will be processed on this stream,
  // therefore references to dynamic table entries shall not prevent their
  // eviction.
  // This method should be called regardless of whether a header block is being
  // decoded on that stream, because a header block might be in flight from the
  // peer.
  // This method should be called every time a request or push stream is reset
  // for any reason: for example, client cancels request, or a decoding error
  // occurs and HeadersHandlerInterface::OnDecodingErrorDetected() is called.
  // This method should also be called if the stream is reset by the peer,
  // because the peer's encoder can only evict entries referenced by header
  // blocks once it receives acknowledgement from this endpoint that the stream
  // is reset.
  // However, this method should not be called if the stream is closed normally
  // using the FIN bit.
  void OnStreamReset(QuicStreamId stream_id);

  // QpackProgressiveDecoder::BlockedStreamLimitEnforcer implementation.
  bool OnStreamBlocked(QuicStreamId stream_id) override;
  void OnStreamUnblocked(QuicStreamId stream_id) override;

  // QpackProgressiveDecoder::DecodingCompletedVisitor implementation.
  void OnDecodingCompleted(QuicStreamId stream_id,
                           uint64_t required_insert_count) override;

  // Factory method to create a QpackProgressiveDecoder for decoding a header
  // block.  |handler| must remain valid until the returned
  // QpackProgressiveDecoder instance is destroyed or the decoder calls
  // |handler->OnHeaderBlockEnd()|.
  std::unique_ptr<QpackProgressiveDecoder> CreateProgressiveDecoder(
      QuicStreamId stream_id,
      QpackProgressiveDecoder::HeadersHandlerInterface* handler);

  // QpackEncoderStreamReceiver::Delegate implementation
  void OnInsertWithNameReference(bool is_static,
                                 uint64_t name_index,
                                 quiche::QuicheStringPiece value) override;
  void OnInsertWithoutNameReference(quiche::QuicheStringPiece name,
                                    quiche::QuicheStringPiece value) override;
  void OnDuplicate(uint64_t index) override;
  void OnSetDynamicTableCapacity(uint64_t capacity) override;
  void OnErrorDetected(quiche::QuicheStringPiece error_message) override;

  // delegate must be set if dynamic table capacity is not zero.
  void set_qpack_stream_sender_delegate(QpackStreamSenderDelegate* delegate) {
    decoder_stream_sender_.set_qpack_stream_sender_delegate(delegate);
  }

  QpackStreamReceiver* encoder_stream_receiver() {
    return &encoder_stream_receiver_;
  }

  // True if any dynamic table entries have been referenced from a header block.
  bool dynamic_table_entry_referenced() const {
    return header_table_.dynamic_table_entry_referenced();
  }

 private:
  EncoderStreamErrorDelegate* const encoder_stream_error_delegate_;
  QpackEncoderStreamReceiver encoder_stream_receiver_;
  QpackDecoderStreamSender decoder_stream_sender_;
  QpackHeaderTable header_table_;
  std::set<QuicStreamId> blocked_streams_;
  const uint64_t maximum_blocked_streams_;

  // Known Received Count is the number of insertions the encoder has received
  // acknowledgement for (through Header Acknowledgement and Insert Count
  // Increment instructions).  The encoder must keep track of it in order to be
  // able to send Insert Count Increment instructions.  See
  // https://quicwg.org/base-drafts/draft-ietf-quic-qpack.html#known-received-count.
  uint64_t known_received_count_;
};

}  // namespace quic

#endif  // QUICHE_QUIC_CORE_QPACK_QPACK_DECODER_H_
