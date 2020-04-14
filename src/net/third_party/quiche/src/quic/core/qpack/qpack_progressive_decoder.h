// Copyright (c) 2018 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef QUICHE_QUIC_CORE_QPACK_QPACK_PROGRESSIVE_DECODER_H_
#define QUICHE_QUIC_CORE_QPACK_QPACK_PROGRESSIVE_DECODER_H_

#include <cstdint>
#include <memory>
#include <string>

#include "net/third_party/quiche/src/quic/core/qpack/qpack_encoder_stream_receiver.h"
#include "net/third_party/quiche/src/quic/core/qpack/qpack_header_table.h"
#include "net/third_party/quiche/src/quic/core/qpack/qpack_instruction_decoder.h"
#include "net/third_party/quiche/src/quic/core/quic_types.h"
#include "net/third_party/quiche/src/quic/platform/api/quic_export.h"
#include "net/third_party/quiche/src/common/platform/api/quiche_string_piece.h"

namespace quic {

class QpackHeaderTable;

// Class to decode a single header block.
class QUIC_EXPORT_PRIVATE QpackProgressiveDecoder
    : public QpackInstructionDecoder::Delegate,
      public QpackHeaderTable::Observer {
 public:
  // Interface for receiving decoded header block from the decoder.
  class QUIC_EXPORT_PRIVATE HeadersHandlerInterface {
   public:
    virtual ~HeadersHandlerInterface() {}

    // Called when a new header name-value pair is decoded.  Multiple values for
    // a given name will be emitted as multiple calls to OnHeader.
    virtual void OnHeaderDecoded(quiche::QuicheStringPiece name,
                                 quiche::QuicheStringPiece value) = 0;

    // Called when the header block is completely decoded.
    // Indicates the total number of bytes in this block.
    // The decoder will not access the handler after this call.
    // Note that this method might not be called synchronously when the header
    // block is received on the wire, in case decoding is blocked on receiving
    // entries on the encoder stream.
    virtual void OnDecodingCompleted() = 0;

    // Called when a decoding error has occurred.  No other methods will be
    // called afterwards.  Implementations are allowed to destroy
    // the QpackProgressiveDecoder instance synchronously.
    virtual void OnDecodingErrorDetected(
        quiche::QuicheStringPiece error_message) = 0;
  };

  // Interface for keeping track of blocked streams for the purpose of enforcing
  // the limit communicated to peer via QPACK_BLOCKED_STREAMS settings.
  class QUIC_EXPORT_PRIVATE BlockedStreamLimitEnforcer {
   public:
    virtual ~BlockedStreamLimitEnforcer() {}

    // Called when the stream becomes blocked.  Returns true if allowed. Returns
    // false if limit is violated, in which case QpackProgressiveDecoder signals
    // an error.
    // Stream must not be already blocked.
    virtual bool OnStreamBlocked(QuicStreamId stream_id) = 0;

    // Called when the stream becomes unblocked.
    // Stream must be blocked.
    virtual void OnStreamUnblocked(QuicStreamId stream_id) = 0;
  };

  // Visitor to be notified when decoding is completed.
  class QUIC_EXPORT_PRIVATE DecodingCompletedVisitor {
   public:
    virtual ~DecodingCompletedVisitor() = default;

    // Called when decoding is completed, with Required Insert Count of the
    // decoded header block.  Required Insert Count is defined at
    // https://quicwg.org/base-drafts/draft-ietf-quic-qpack.html#blocked-streams.
    virtual void OnDecodingCompleted(QuicStreamId stream_id,
                                     uint64_t required_insert_count) = 0;
  };

  QpackProgressiveDecoder() = delete;
  QpackProgressiveDecoder(QuicStreamId stream_id,
                          BlockedStreamLimitEnforcer* enforcer,
                          DecodingCompletedVisitor* visitor,
                          QpackHeaderTable* header_table,
                          HeadersHandlerInterface* handler);
  QpackProgressiveDecoder(const QpackProgressiveDecoder&) = delete;
  QpackProgressiveDecoder& operator=(const QpackProgressiveDecoder&) = delete;
  ~QpackProgressiveDecoder() override;

  // Provide a data fragment to decode.
  void Decode(quiche::QuicheStringPiece data);

  // Signal that the entire header block has been received and passed in
  // through Decode().  No methods must be called afterwards.
  void EndHeaderBlock();

  // QpackInstructionDecoder::Delegate implementation.
  bool OnInstructionDecoded(const QpackInstruction* instruction) override;
  void OnError(quiche::QuicheStringPiece error_message) override;

  // QpackHeaderTable::Observer implementation.
  void OnInsertCountReachedThreshold() override;
  void Cancel() override;

 private:
  bool DoIndexedHeaderFieldInstruction();
  bool DoIndexedHeaderFieldPostBaseInstruction();
  bool DoLiteralHeaderFieldNameReferenceInstruction();
  bool DoLiteralHeaderFieldPostBaseInstruction();
  bool DoLiteralHeaderFieldInstruction();
  bool DoPrefixInstruction();

  // Called as soon as EndHeaderBlock() is called and decoding is not blocked.
  void FinishDecoding();

  // Calculates Base from |required_insert_count_|, which must be set before
  // calling this method, and sign bit and Delta Base in the Header Data Prefix,
  // which are passed in as arguments.  Returns true on success, false on
  // failure due to overflow/underflow.
  bool DeltaBaseToBase(bool sign, uint64_t delta_base, uint64_t* base);

  const QuicStreamId stream_id_;

  // |prefix_decoder_| only decodes a handful of bytes then it can be
  // destroyed to conserve memory.  |instruction_decoder_|, on the other hand,
  // is used until the entire header block is decoded.
  std::unique_ptr<QpackInstructionDecoder> prefix_decoder_;
  QpackInstructionDecoder instruction_decoder_;

  BlockedStreamLimitEnforcer* const enforcer_;
  DecodingCompletedVisitor* const visitor_;
  QpackHeaderTable* const header_table_;
  HeadersHandlerInterface* const handler_;

  // Required Insert Count and Base are decoded from the Header Data Prefix.
  uint64_t required_insert_count_;
  uint64_t base_;

  // Required Insert Count is one larger than the largest absolute index of all
  // referenced dynamic table entries, or zero if no dynamic table entries are
  // referenced.  |required_insert_count_so_far_| starts out as zero and keeps
  // track of the Required Insert Count based on entries decoded so far.
  // After decoding is completed, it is compared to |required_insert_count_|.
  uint64_t required_insert_count_so_far_;

  // False until prefix is fully read and decoded.
  bool prefix_decoded_;

  // True if waiting for dynamic table entries to arrive.
  bool blocked_;

  // Buffer the entire header block after the prefix while decoding is blocked.
  std::string buffer_;

  // True until EndHeaderBlock() is called.
  bool decoding_;

  // True if a decoding error has been detected.
  bool error_detected_;

  // True if QpackHeaderTable has been destroyed
  // while decoding is still blocked.
  bool cancelled_;
};

}  // namespace quic

#endif  // QUICHE_QUIC_CORE_QPACK_QPACK_PROGRESSIVE_DECODER_H_
