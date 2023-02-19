// Copyright (c) 2019 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef QUICHE_QUIC_CORE_QPACK_QPACK_DECODED_HEADERS_ACCUMULATOR_H_
#define QUICHE_QUIC_CORE_QPACK_QPACK_DECODED_HEADERS_ACCUMULATOR_H_

#include <cstddef>
#include <string>

#include "absl/strings/string_view.h"
#include "quiche/quic/core/http/quic_header_list.h"
#include "quiche/quic/core/qpack/qpack_progressive_decoder.h"
#include "quiche/quic/core/quic_error_codes.h"
#include "quiche/quic/core/quic_types.h"
#include "quiche/quic/platform/api/quic_export.h"

namespace quic {

class QpackDecoder;

// A class that creates and owns a QpackProgressiveDecoder instance, accumulates
// decoded headers in a QuicHeaderList, and keeps track of uncompressed and
// compressed size so that it can be passed to
// QuicHeaderList::OnHeaderBlockEnd().
class QUIC_EXPORT_PRIVATE QpackDecodedHeadersAccumulator
    : public QpackProgressiveDecoder::HeadersHandlerInterface {
 public:
  // Visitor interface to signal success or error.
  // Exactly one method will be called.
  // Methods may be called synchronously from Decode() and EndHeaderBlock(),
  // or asynchronously.
  // Method implementations are allowed to destroy |this|.
  class QUIC_EXPORT_PRIVATE Visitor {
   public:
    virtual ~Visitor() = default;

    // Called when headers are successfully decoded.  If the uncompressed header
    // list size including an overhead for each header field exceeds the limit
    // specified via |max_header_list_size| in QpackDecodedHeadersAccumulator
    // constructor, then |header_list_size_limit_exceeded| will be true, and
    // |headers| will be empty but will still have the correct compressed and
    // uncompressed size
    // information.
    virtual void OnHeadersDecoded(QuicHeaderList headers,
                                  bool header_list_size_limit_exceeded) = 0;

    // Called when an error has occurred.
    virtual void OnHeaderDecodingError(QuicErrorCode error_code,
                                       absl::string_view error_message) = 0;
  };

  QpackDecodedHeadersAccumulator(QuicStreamId id, QpackDecoder* qpack_decoder,
                                 Visitor* visitor, size_t max_header_list_size);
  virtual ~QpackDecodedHeadersAccumulator() = default;

  // QpackProgressiveDecoder::HeadersHandlerInterface implementation.
  // These methods should only be called by |decoder_|.
  void OnHeaderDecoded(absl::string_view name,
                       absl::string_view value) override;
  void OnDecodingCompleted() override;
  void OnDecodingErrorDetected(QuicErrorCode error_code,
                               absl::string_view error_message) override;

  // Decode payload data.
  // Must not be called if an error has been detected.
  // Must not be called after EndHeaderBlock().
  void Decode(absl::string_view data);

  // Signal end of HEADERS frame.
  // Must not be called if an error has been detected.
  // Must not be called more that once.
  void EndHeaderBlock();

 private:
  std::unique_ptr<QpackProgressiveDecoder> decoder_;
  Visitor* visitor_;
  // Maximum header list size including overhead.
  size_t max_header_list_size_;
  // Uncompressed header list size including overhead, for enforcing the limit.
  size_t uncompressed_header_bytes_including_overhead_;
  QuicHeaderList quic_header_list_;
  // Uncompressed header list size with overhead,
  // for passing in to QuicHeaderList::OnHeaderBlockEnd().
  size_t uncompressed_header_bytes_without_overhead_;
  // Compressed header list size
  // for passing in to QuicHeaderList::OnHeaderBlockEnd().
  size_t compressed_header_bytes_;

  // True if the header size limit has been exceeded.
  // Input data is still fed to QpackProgressiveDecoder.
  bool header_list_size_limit_exceeded_;

  // The following two members are only used for QUICHE_DCHECKs.

  // True if headers have been completedly and successfully decoded.
  bool headers_decoded_;
  // True if an error has been detected during decoding.
  bool error_detected_;
};

}  // namespace quic

#endif  // QUICHE_QUIC_CORE_QPACK_QPACK_DECODED_HEADERS_ACCUMULATOR_H_
