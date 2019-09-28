// Copyright (c) 2019 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef QUICHE_QUIC_CORE_QPACK_QPACK_DECODED_HEADERS_ACCUMULATOR_H_
#define QUICHE_QUIC_CORE_QPACK_QPACK_DECODED_HEADERS_ACCUMULATOR_H_

#include <cstddef>
#include <string>

#include "net/third_party/quiche/src/quic/core/http/quic_header_list.h"
#include "net/third_party/quiche/src/quic/core/qpack/qpack_progressive_decoder.h"
#include "net/third_party/quiche/src/quic/core/quic_types.h"
#include "net/third_party/quiche/src/quic/platform/api/quic_export.h"
#include "net/third_party/quiche/src/quic/platform/api/quic_string_piece.h"

namespace quic {

class QpackDecoder;

// A class that creates and owns a QpackProgressiveDecoder instance, accumulates
// decoded headers in a QuicHeaderList, and keeps track of uncompressed and
// compressed size so that it can be passed to QuicHeaderList::EndHeaderBlock().
class QUIC_EXPORT_PRIVATE QpackDecodedHeadersAccumulator
    : public QpackProgressiveDecoder::HeadersHandlerInterface {
 public:
  // Return value for EndHeaderBlock().
  enum class Status {
    // Headers have been successfully decoded.
    kSuccess,
    // An error has occurred.
    kError,
    // Decoding is blocked.
    kBlocked
  };

  // Visitor interface used for blocked decoding.  Exactly one visitor method
  // will be called if EndHeaderBlock() returned kBlocked.  No visitor method
  // will be called if EndHeaderBlock() returned any other value.
  class Visitor {
   public:
    virtual ~Visitor() = default;

    // Called when headers are successfully decoded.
    virtual void OnHeadersDecoded(QuicHeaderList headers) = 0;

    // Called when an error has occurred.
    virtual void OnHeaderDecodingError() = 0;
  };

  QpackDecodedHeadersAccumulator(QuicStreamId id,
                                 QpackDecoder* qpack_decoder,
                                 Visitor* visitor,
                                 size_t max_header_list_size);
  virtual ~QpackDecodedHeadersAccumulator() = default;

  // QpackProgressiveDecoder::HeadersHandlerInterface implementation.
  // These methods should only be called by |decoder_|.
  void OnHeaderDecoded(QuicStringPiece name, QuicStringPiece value) override;
  void OnDecodingCompleted() override;
  void OnDecodingErrorDetected(QuicStringPiece error_message) override;

  // Decode payload data.  Returns true on success, false on error.
  // Must not be called if an error has been detected.
  // Must not be called after EndHeaderBlock().
  bool Decode(QuicStringPiece data);

  // Signal end of HEADERS frame.
  // Must not be called if an error has been detected.
  // Must not be called more that once.
  // Returns kSuccess if headers can be readily decoded.
  // Returns kError if an error occurred.
  // Returns kBlocked if headers cannot be decoded at the moment, in which case
  // exactly one Visitor method will be called as soon as sufficient data
  // is received on the QPACK decoder stream.
  Status EndHeaderBlock();

  // Returns accumulated header list.
  const QuicHeaderList& quic_header_list() const;

  // Returns error message.
  // Must not be called unless an error has been detected.
  // TODO(b/124216424): Add accessor for error code, return HTTP_EXCESSIVE_LOAD
  // or HTTP_QPACK_DECOMPRESSION_FAILED.
  QuicStringPiece error_message() const;

 private:
  std::unique_ptr<QpackProgressiveDecoder> decoder_;
  Visitor* visitor_;
  QuicHeaderList quic_header_list_;
  size_t uncompressed_header_bytes_;
  size_t compressed_header_bytes_;
  // Set to true when OnDecodingCompleted() is called.
  bool headers_decoded_;
  // Set to true when EndHeaderBlock() returns kBlocked.
  bool blocked_;
  bool error_detected_;
  std::string error_message_;
};

}  // namespace quic

#endif  // QUICHE_QUIC_CORE_QPACK_QPACK_DECODED_HEADERS_ACCUMULATOR_H_
