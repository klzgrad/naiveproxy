// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "quiche/http2/hpack/decoder/hpack_decoder.h"

#include "quiche/http2/decoder/decode_status.h"
#include "quiche/common/platform/api/quiche_flag_utils.h"
#include "quiche/common/platform/api/quiche_logging.h"

namespace http2 {

HpackDecoder::HpackDecoder(HpackDecoderListener* listener,
                           size_t max_string_size)
    : decoder_state_(listener),
      entry_buffer_(&decoder_state_, max_string_size),
      block_decoder_(&entry_buffer_),
      error_(HpackDecodingError::kOk) {}

HpackDecoder::~HpackDecoder() = default;

void HpackDecoder::set_max_string_size_bytes(size_t max_string_size_bytes) {
  entry_buffer_.set_max_string_size_bytes(max_string_size_bytes);
}

void HpackDecoder::ApplyHeaderTableSizeSetting(uint32_t max_header_table_size) {
  decoder_state_.ApplyHeaderTableSizeSetting(max_header_table_size);
}

bool HpackDecoder::StartDecodingBlock() {
  QUICHE_DVLOG(3) << "HpackDecoder::StartDecodingBlock, error_detected="
                  << (DetectError() ? "true" : "false");
  if (DetectError()) {
    return false;
  }
  // TODO(jamessynge): Eliminate Reset(), which shouldn't be necessary
  // if there are no errors, and shouldn't be necessary with errors if
  // we never resume decoding after an error has been detected.
  block_decoder_.Reset();
  decoder_state_.OnHeaderBlockStart();
  return true;
}

bool HpackDecoder::DecodeFragment(DecodeBuffer* db) {
  QUICHE_DVLOG(3) << "HpackDecoder::DecodeFragment, error_detected="
                  << (DetectError() ? "true" : "false")
                  << ", size=" << db->Remaining();
  if (DetectError()) {
    QUICHE_CODE_COUNT_N(decompress_failure_3, 3, 23);
    return false;
  }
  // Decode contents of db as an HPACK block fragment, forwards the decoded
  // entries to entry_buffer_, which in turn forwards them to decode_state_,
  // which finally forwards them to the HpackDecoderListener.
  DecodeStatus status = block_decoder_.Decode(db);
  if (status == DecodeStatus::kDecodeError) {
    ReportError(block_decoder_.error(), "");
    QUICHE_CODE_COUNT_N(decompress_failure_3, 4, 23);
    return false;
  } else if (DetectError()) {
    QUICHE_CODE_COUNT_N(decompress_failure_3, 5, 23);
    return false;
  }
  // Should be positioned between entries iff decoding is complete.
  QUICHE_DCHECK_EQ(block_decoder_.before_entry(),
                   status == DecodeStatus::kDecodeDone)
      << status;
  if (!block_decoder_.before_entry()) {
    entry_buffer_.BufferStringsIfUnbuffered();
  }
  return true;
}

bool HpackDecoder::EndDecodingBlock() {
  QUICHE_DVLOG(3) << "HpackDecoder::EndDecodingBlock, error_detected="
                  << (DetectError() ? "true" : "false");
  if (DetectError()) {
    QUICHE_CODE_COUNT_N(decompress_failure_3, 6, 23);
    return false;
  }
  if (!block_decoder_.before_entry()) {
    // The HPACK block ended in the middle of an entry.
    ReportError(HpackDecodingError::kTruncatedBlock, "");
    QUICHE_CODE_COUNT_N(decompress_failure_3, 7, 23);
    return false;
  }
  decoder_state_.OnHeaderBlockEnd();
  if (DetectError()) {
    // HpackDecoderState will have reported the error.
    QUICHE_CODE_COUNT_N(decompress_failure_3, 8, 23);
    return false;
  }
  return true;
}

bool HpackDecoder::DetectError() {
  if (error_ != HpackDecodingError::kOk) {
    return true;
  }

  if (decoder_state_.error() != HpackDecodingError::kOk) {
    QUICHE_DVLOG(2) << "Error detected in decoder_state_";
    QUICHE_CODE_COUNT_N(decompress_failure_3, 10, 23);
    error_ = decoder_state_.error();
    detailed_error_ = decoder_state_.detailed_error();
  }

  return error_ != HpackDecodingError::kOk;
}

void HpackDecoder::ReportError(HpackDecodingError error,
                               std::string detailed_error) {
  QUICHE_DVLOG(3) << "HpackDecoder::ReportError is new="
                  << (error_ == HpackDecodingError::kOk ? "true" : "false")
                  << ", error: " << HpackDecodingErrorToString(error);
  if (error_ == HpackDecodingError::kOk) {
    error_ = error;
    detailed_error_ = detailed_error;
    decoder_state_.listener()->OnHeaderErrorDetected(
        HpackDecodingErrorToString(error));
  }
}

}  // namespace http2
