// Copyright (c) 2019 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef NET_THIRD_PARTY_QUIC_CORE_QPACK_QPACK_DECODED_HEADERS_ACCUMULATOR_H_
#define NET_THIRD_PARTY_QUIC_CORE_QPACK_QPACK_DECODED_HEADERS_ACCUMULATOR_H_

#include <cstddef>

#include "net/third_party/quic/core/http/quic_header_list.h"
#include "net/third_party/quic/core/qpack/qpack_progressive_decoder.h"
#include "net/third_party/quic/core/quic_types.h"
#include "net/third_party/quic/platform/api/quic_export.h"
#include "net/third_party/quic/platform/api/quic_string.h"
#include "net/third_party/quic/platform/api/quic_string_piece.h"

namespace quic {

class QpackDecoder;

// A class that creates and owns a QpackProgressiveDecoder instance, accumulates
// decoded headers in a QuicHeaderList, and keeps track of uncompressed and
// compressed size so that it can be passed to QuicHeaderList::EndHeaderBlock().
class QUIC_EXPORT_PRIVATE QpackDecodedHeadersAccumulator
    : public QpackProgressiveDecoder::HeadersHandlerInterface {
 public:
  QpackDecodedHeadersAccumulator(QuicStreamId id, QpackDecoder* qpack_decoder);
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

  // Signal end of HEADERS frame.  Returns true on success, false on error.
  // Must not be called if an error has been detected.
  // Must not be called more that once.
  bool EndHeaderBlock();

  // Returns accumulated header list.
  const QuicHeaderList& quic_header_list() const;

  // Returns error message.
  // Must not be called unless an error has been detected.
  QuicStringPiece error_message() const;

 private:
  std::unique_ptr<QpackProgressiveDecoder> decoder_;
  QuicHeaderList quic_header_list_;
  size_t uncompressed_header_bytes_;
  size_t compressed_header_bytes_;
  bool error_detected_;
  QuicString error_message_;
};

}  // namespace quic

#endif  // NET_THIRD_PARTY_QUIC_CORE_QPACK_QPACK_DECODED_HEADERS_ACCUMULATOR_H_
