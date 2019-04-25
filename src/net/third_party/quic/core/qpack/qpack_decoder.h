// Copyright (c) 2018 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef NET_THIRD_PARTY_QUIC_CORE_QPACK_QPACK_DECODER_H_
#define NET_THIRD_PARTY_QUIC_CORE_QPACK_QPACK_DECODER_H_

#include <cstdint>
#include <memory>

#include "net/third_party/quic/core/qpack/qpack_decoder_stream_sender.h"
#include "net/third_party/quic/core/qpack/qpack_encoder_stream_receiver.h"
#include "net/third_party/quic/core/qpack/qpack_header_table.h"
#include "net/third_party/quic/core/qpack/qpack_progressive_decoder.h"
#include "net/third_party/quic/core/quic_types.h"
#include "net/third_party/quic/platform/api/quic_export.h"
#include "net/third_party/quic/platform/api/quic_string_piece.h"

namespace quic {

// QPACK decoder class.  Exactly one instance should exist per QUIC connection.
// This class vends a new QpackProgressiveDecoder instance for each new header
// list to be encoded.
class QUIC_EXPORT_PRIVATE QpackDecoder
    : public QpackEncoderStreamReceiver::Delegate {
 public:
  // Interface for receiving notification that an error has occurred on the
  // encoder stream.  This MUST be treated as a connection error of type
  // HTTP_QPACK_ENCODER_STREAM_ERROR.
  class QUIC_EXPORT_PRIVATE EncoderStreamErrorDelegate {
   public:
    virtual ~EncoderStreamErrorDelegate() {}

    virtual void OnEncoderStreamError(QuicStringPiece error_message) = 0;
  };

  QpackDecoder(
      EncoderStreamErrorDelegate* encoder_stream_error_delegate,
      QpackDecoderStreamSender::Delegate* decoder_stream_sender_delegate);
  ~QpackDecoder() override;

  // Set maximum capacity of dynamic table.
  // This method must only be called at most once.
  void SetMaximumDynamicTableCapacity(uint64_t maximum_dynamic_table_capacity);

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

  // Factory method to create a QpackProgressiveDecoder for decoding a header
  // block.  |handler| must remain valid until the returned
  // QpackProgressiveDecoder instance is destroyed or the decoder calls
  // |handler->OnHeaderBlockEnd()|.
  std::unique_ptr<QpackProgressiveDecoder> DecodeHeaderBlock(
      QuicStreamId stream_id,
      QpackProgressiveDecoder::HeadersHandlerInterface* handler);

  // Decode data received on the encoder stream.
  void DecodeEncoderStreamData(QuicStringPiece data);

  // QpackEncoderStreamReceiver::Delegate implementation
  void OnInsertWithNameReference(bool is_static,
                                 uint64_t name_index,
                                 QuicStringPiece value) override;
  void OnInsertWithoutNameReference(QuicStringPiece name,
                                    QuicStringPiece value) override;
  void OnDuplicate(uint64_t index) override;
  void OnSetDynamicTableCapacity(uint64_t capacity) override;
  void OnErrorDetected(QuicStringPiece error_message) override;

 private:
  // The encoder stream uses relative index (but different from the kind of
  // relative index used on a request stream).  This method converts relative
  // index to absolute index (zero based).  It returns true on success, or false
  // if conversion fails due to overflow/underflow.
  bool EncoderStreamRelativeIndexToAbsoluteIndex(
      uint64_t relative_index,
      uint64_t* absolute_index) const;

  EncoderStreamErrorDelegate* const encoder_stream_error_delegate_;
  QpackEncoderStreamReceiver encoder_stream_receiver_;
  QpackDecoderStreamSender decoder_stream_sender_;
  QpackHeaderTable header_table_;
};

}  // namespace quic

#endif  // NET_THIRD_PARTY_QUIC_CORE_QPACK_QPACK_DECODER_H_
