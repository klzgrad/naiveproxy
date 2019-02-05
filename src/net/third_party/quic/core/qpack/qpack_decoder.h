// Copyright (c) 2018 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef NET_THIRD_PARTY_QUIC_CORE_QPACK_QPACK_DECODER_H_
#define NET_THIRD_PARTY_QUIC_CORE_QPACK_QPACK_DECODER_H_

#include <memory>

#include "net/third_party/quic/core/qpack/qpack_header_table.h"
#include "net/third_party/quic/core/qpack/qpack_instruction_decoder.h"
#include "net/third_party/quic/platform/api/quic_export.h"
#include "net/third_party/quic/platform/api/quic_string.h"
#include "net/third_party/quic/platform/api/quic_string_piece.h"

namespace quic {

// QPACK decoder class.  Exactly one instance should exist per QUIC connection.
// This class vends a new ProgressiveDecoder instance for each new header list
// to be encoded.
// TODO(bnc): This class will manage the decoding context, send data on the
// decoder stream, and receive data on the encoder stream.
class QUIC_EXPORT_PRIVATE QpackDecoder {
 public:
  // Interface for receiving decoded header block from the decoder.
  class QUIC_EXPORT_PRIVATE HeadersHandlerInterface {
   public:
    virtual ~HeadersHandlerInterface() {}

    // Called when a new header name-value pair is decoded.  Multiple values for
    // a given name will be emitted as multiple calls to OnHeader.
    virtual void OnHeaderDecoded(QuicStringPiece name,
                                 QuicStringPiece value) = 0;

    // Called when the header block is completely decoded.
    // Indicates the total number of bytes in this block.
    // The decoder will not access the handler after this call.
    // Note that this method might not be called synchronously when the header
    // block is received on the wire, in case decoding is blocked on receiving
    // entries on the encoder stream.  TODO(bnc): Implement blocked decoding.
    virtual void OnDecodingCompleted() = 0;

    // Called when a decoding error has occurred.  No other methods will be
    // called afterwards.
    virtual void OnDecodingErrorDetected(QuicStringPiece error_message) = 0;
  };

  // Class to decode a single header block.
  class QUIC_EXPORT_PRIVATE ProgressiveDecoder
      : public QpackInstructionDecoder::Delegate {
   public:
    ProgressiveDecoder() = delete;
    ProgressiveDecoder(QpackHeaderTable* header_table,
                       HeadersHandlerInterface* handler);
    ProgressiveDecoder(const ProgressiveDecoder&) = delete;
    ProgressiveDecoder& operator=(const ProgressiveDecoder&) = delete;
    ~ProgressiveDecoder() override = default;

    // Provide a data fragment to decode.
    void Decode(QuicStringPiece data);

    // Signal that the entire header block has been received and passed in
    // through Decode().  No methods must be called afterwards.
    void EndHeaderBlock();

    // QpackInstructionDecoder::Delegate implementation.
    bool OnInstructionDecoded(const QpackInstruction* instruction) override;
    void OnError(QuicStringPiece error_message) override;

   private:
    QpackInstructionDecoder instruction_decoder_;
    const QpackHeaderTable* const header_table_;
    HeadersHandlerInterface* handler_;

    // True until EndHeaderBlock() is called.
    bool decoding_;

    // True if a decoding error has been detected.
    bool error_detected_;
  };

  // Factory method to create a ProgressiveDecoder for decoding a header block.
  // |handler| must remain valid until the returned ProgressiveDecoder instance
  // is destroyed or the decoder calls |handler->OnHeaderBlockEnd()|.
  std::unique_ptr<ProgressiveDecoder> DecodeHeaderBlock(
      HeadersHandlerInterface* handler);

 private:
  QpackHeaderTable header_table_;
};

}  // namespace quic

#endif  // NET_THIRD_PARTY_QUIC_CORE_QPACK_QPACK_DECODER_H_
