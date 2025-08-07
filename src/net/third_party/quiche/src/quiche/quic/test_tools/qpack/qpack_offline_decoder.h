// Copyright (c) 2018 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef QUICHE_QUIC_TEST_TOOLS_QPACK_QPACK_OFFLINE_DECODER_H_
#define QUICHE_QUIC_TEST_TOOLS_QPACK_QPACK_OFFLINE_DECODER_H_

#include <list>

#include "absl/strings/string_view.h"
#include "quiche/quic/core/qpack/qpack_decoder.h"
#include "quiche/quic/core/quic_error_codes.h"
#include "quiche/quic/test_tools/qpack/qpack_decoder_test_utils.h"
#include "quiche/quic/test_tools/qpack/qpack_test_utils.h"
#include "quiche/common/http/http_header_block.h"

namespace quic {

// A decoder to read encoded data from a file, decode it, and compare to
// a list of expected header lists read from another file.  File format is
// described at
// https://github.com/quicwg/base-drafts/wiki/QPACK-Offline-Interop.
class QpackOfflineDecoder : public QpackDecoder::EncoderStreamErrorDelegate {
 public:
  QpackOfflineDecoder();
  ~QpackOfflineDecoder() override = default;

  // Read encoded header blocks and encoder stream data from |input_filename|
  // and decode them, read expected header lists from
  // |expected_headers_filename|, and compare decoded header lists to expected
  // ones.  Returns true if there is an equal number of them and the
  // corresponding ones match, false otherwise.
  bool DecodeAndVerifyOfflineData(absl::string_view input_filename,
                                  absl::string_view expected_headers_filename);

  // QpackDecoder::EncoderStreamErrorDelegate implementation:
  void OnEncoderStreamError(QuicErrorCode error_code,
                            absl::string_view error_message) override;

 private:
  // Data structure to hold TestHeadersHandler and QpackProgressiveDecoder until
  // decoding of a header header block (and all preceding header blocks) is
  // complete.
  struct Decoder {
    std::unique_ptr<test::TestHeadersHandler> headers_handler;
    std::unique_ptr<QpackProgressiveDecoder> progressive_decoder;
    uint64_t stream_id;
  };

  // Parse decoder parameters from |input_filename| and set up |qpack_decoder_|
  // accordingly.
  bool ParseInputFilename(absl::string_view input_filename);

  // Read encoded header blocks and encoder stream data from |input_filename|,
  // pass them to |qpack_decoder_| for decoding, and add decoded header lists to
  // |decoded_header_lists_|.
  bool DecodeHeaderBlocksFromFile(absl::string_view input_filename);

  // Read expected header lists from |expected_headers_filename| and verify
  // decoded header lists in |decoded_header_lists_| against them.
  bool VerifyDecodedHeaderLists(absl::string_view expected_headers_filename);

  // Parse next header list from |*expected_headers_data| into
  // |*expected_header_list|, removing consumed data from the beginning of
  // |*expected_headers_data|.  Returns true on success, false if parsing fails.
  bool ReadNextExpectedHeaderList(
      absl::string_view* expected_headers_data,
      quiche::HttpHeaderBlock* expected_header_list);

  // Compare two header lists.  Allow for different orders of certain headers as
  // described at
  // https://github.com/qpackers/qifs/blob/master/encoded/qpack-03/h2o/README.md.
  bool CompareHeaderBlocks(quiche::HttpHeaderBlock decoded_header_list,
                           quiche::HttpHeaderBlock expected_header_list);

  bool encoder_stream_error_detected_;
  test::NoopQpackStreamSenderDelegate decoder_stream_sender_delegate_;
  std::unique_ptr<QpackDecoder> qpack_decoder_;

  // Objects necessary for decoding, one list element for each header block.
  std::list<Decoder> decoders_;

  // Decoded header lists.
  std::list<quiche::HttpHeaderBlock> decoded_header_lists_;
};

}  // namespace quic

#endif  // QUICHE_QUIC_TEST_TOOLS_QPACK_QPACK_OFFLINE_DECODER_H_
