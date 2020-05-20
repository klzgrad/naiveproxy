// Copyright (c) 2018 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef QUICHE_QUIC_TEST_TOOLS_QPACK_QPACK_DECODER_TEST_UTILS_H_
#define QUICHE_QUIC_TEST_TOOLS_QPACK_QPACK_DECODER_TEST_UTILS_H_

#include <string>

#include "net/third_party/quiche/src/quic/core/qpack/qpack_decoder.h"
#include "net/third_party/quiche/src/quic/core/qpack/qpack_progressive_decoder.h"
#include "net/third_party/quiche/src/quic/platform/api/quic_test.h"
#include "net/third_party/quiche/src/quic/test_tools/qpack/qpack_test_utils.h"
#include "net/third_party/quiche/src/common/platform/api/quiche_string_piece.h"
#include "net/third_party/quiche/src/spdy/core/spdy_header_block.h"

namespace quic {
namespace test {

// QpackDecoder::EncoderStreamErrorDelegate implementation that does nothing.
class NoopEncoderStreamErrorDelegate
    : public QpackDecoder::EncoderStreamErrorDelegate {
 public:
  ~NoopEncoderStreamErrorDelegate() override = default;

  void OnEncoderStreamError(quiche::QuicheStringPiece error_message) override;
};

// Mock QpackDecoder::EncoderStreamErrorDelegate implementation.
class MockEncoderStreamErrorDelegate
    : public QpackDecoder::EncoderStreamErrorDelegate {
 public:
  ~MockEncoderStreamErrorDelegate() override = default;

  MOCK_METHOD1(OnEncoderStreamError,
               void(quiche::QuicheStringPiece error_message));
};

// HeadersHandlerInterface implementation that collects decoded headers
// into a SpdyHeaderBlock.
class TestHeadersHandler
    : public QpackProgressiveDecoder::HeadersHandlerInterface {
 public:
  TestHeadersHandler();
  ~TestHeadersHandler() override = default;

  // HeadersHandlerInterface implementation:
  void OnHeaderDecoded(quiche::QuicheStringPiece name,
                       quiche::QuicheStringPiece value) override;
  void OnDecodingCompleted() override;
  void OnDecodingErrorDetected(
      quiche::QuicheStringPiece error_message) override;

  // Release decoded header list.  Must only be called if decoding is complete
  // and no errors have been detected.
  spdy::SpdyHeaderBlock ReleaseHeaderList();

  bool decoding_completed() const;
  bool decoding_error_detected() const;
  const std::string& error_message() const;

 private:
  spdy::SpdyHeaderBlock header_list_;
  bool decoding_completed_;
  bool decoding_error_detected_;
  std::string error_message_;
};

class MockHeadersHandler
    : public QpackProgressiveDecoder::HeadersHandlerInterface {
 public:
  MockHeadersHandler() = default;
  MockHeadersHandler(const MockHeadersHandler&) = delete;
  MockHeadersHandler& operator=(const MockHeadersHandler&) = delete;
  ~MockHeadersHandler() override = default;

  MOCK_METHOD2(OnHeaderDecoded,
               void(quiche::QuicheStringPiece name,
                    quiche::QuicheStringPiece value));
  MOCK_METHOD0(OnDecodingCompleted, void());
  MOCK_METHOD1(OnDecodingErrorDetected,
               void(quiche::QuicheStringPiece error_message));
};

class NoOpHeadersHandler
    : public QpackProgressiveDecoder::HeadersHandlerInterface {
 public:
  ~NoOpHeadersHandler() override = default;

  void OnHeaderDecoded(quiche::QuicheStringPiece /*name*/,
                       quiche::QuicheStringPiece /*value*/) override {}
  void OnDecodingCompleted() override {}
  void OnDecodingErrorDetected(
      quiche::QuicheStringPiece /*error_message*/) override {}
};

void QpackDecode(
    uint64_t maximum_dynamic_table_capacity,
    uint64_t maximum_blocked_streams,
    QpackDecoder::EncoderStreamErrorDelegate* encoder_stream_error_delegate,
    QpackStreamSenderDelegate* decoder_stream_sender_delegate,
    QpackProgressiveDecoder::HeadersHandlerInterface* handler,
    const FragmentSizeGenerator& fragment_size_generator,
    quiche::QuicheStringPiece data);

}  // namespace test
}  // namespace quic

#endif  // QUICHE_QUIC_TEST_TOOLS_QPACK_QPACK_DECODER_TEST_UTILS_H_
