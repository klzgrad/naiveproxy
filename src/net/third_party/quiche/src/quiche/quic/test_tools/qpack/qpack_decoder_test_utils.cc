// Copyright (c) 2018 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "quiche/quic/test_tools/qpack/qpack_decoder_test_utils.h"

#include <algorithm>
#include <cstddef>
#include <string>
#include <utility>

#include "absl/strings/string_view.h"
#include "quiche/quic/platform/api/quic_test.h"

namespace quic {
namespace test {

TestHeadersHandler::TestHeadersHandler()
    : decoding_completed_(false), decoding_error_detected_(false) {}

void TestHeadersHandler::OnHeaderDecoded(absl::string_view name,
                                         absl::string_view value) {
  ASSERT_FALSE(decoding_completed_);
  ASSERT_FALSE(decoding_error_detected_);

  header_list_.AppendValueOrAddHeader(name, value);
}

void TestHeadersHandler::OnDecodingCompleted() {
  ASSERT_FALSE(decoding_completed_);
  ASSERT_FALSE(decoding_error_detected_);

  decoding_completed_ = true;
}

void TestHeadersHandler::OnDecodingErrorDetected(
    QuicErrorCode /*error_code*/, absl::string_view error_message) {
  ASSERT_FALSE(decoding_completed_);
  ASSERT_FALSE(decoding_error_detected_);

  decoding_error_detected_ = true;
  error_message_.assign(error_message.data(), error_message.size());
}

quiche::HttpHeaderBlock TestHeadersHandler::ReleaseHeaderList() {
  QUICHE_DCHECK(decoding_completed_);
  QUICHE_DCHECK(!decoding_error_detected_);

  return std::move(header_list_);
}

bool TestHeadersHandler::decoding_completed() const {
  return decoding_completed_;
}

bool TestHeadersHandler::decoding_error_detected() const {
  return decoding_error_detected_;
}

const std::string& TestHeadersHandler::error_message() const {
  QUICHE_DCHECK(decoding_error_detected_);
  return error_message_;
}

void QpackDecode(
    uint64_t maximum_dynamic_table_capacity, uint64_t maximum_blocked_streams,
    QpackDecoder::EncoderStreamErrorDelegate* encoder_stream_error_delegate,
    QpackStreamSenderDelegate* decoder_stream_sender_delegate,
    QpackProgressiveDecoder::HeadersHandlerInterface* handler,
    const FragmentSizeGenerator& fragment_size_generator,
    absl::string_view data) {
  QpackDecoder decoder(maximum_dynamic_table_capacity, maximum_blocked_streams,
                       encoder_stream_error_delegate);
  decoder.set_qpack_stream_sender_delegate(decoder_stream_sender_delegate);
  auto progressive_decoder =
      decoder.CreateProgressiveDecoder(/* stream_id = */ 1, handler);
  while (!data.empty()) {
    size_t fragment_size = std::min(fragment_size_generator(), data.size());
    progressive_decoder->Decode(data.substr(0, fragment_size));
    data = data.substr(fragment_size);
  }
  progressive_decoder->EndHeaderBlock();
}

}  // namespace test
}  // namespace quic
