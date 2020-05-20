// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/third_party/quiche/src/http2/hpack/decoder/hpack_decoder.h"

#include "net/third_party/quiche/src/http2/decoder/decode_status.h"
#include "net/third_party/quiche/src/http2/platform/api/http2_estimate_memory_usage.h"
#include "net/third_party/quiche/src/http2/platform/api/http2_flags.h"
#include "net/third_party/quiche/src/http2/platform/api/http2_logging.h"

namespace http2 {

HpackDecoder::HpackDecoder(HpackDecoderListener* listener,
                           size_t max_string_size)
    : decoder_state_(listener),
      entry_buffer_(&decoder_state_, max_string_size),
      block_decoder_(&entry_buffer_),
      error_(HpackDecodingError::kOk),
      http2_skip_querying_entry_buffer_error_(
          GetHttp2ReloadableFlag(http2_skip_querying_entry_buffer_error)) {}

HpackDecoder::~HpackDecoder() = default;

void HpackDecoder::set_tables_debug_listener(
    HpackDecoderTablesDebugListener* debug_listener) {
  decoder_state_.set_tables_debug_listener(debug_listener);
}

void HpackDecoder::set_max_string_size_bytes(size_t max_string_size_bytes) {
  entry_buffer_.set_max_string_size_bytes(max_string_size_bytes);
}

void HpackDecoder::ApplyHeaderTableSizeSetting(uint32_t max_header_table_size) {
  decoder_state_.ApplyHeaderTableSizeSetting(max_header_table_size);
}

bool HpackDecoder::StartDecodingBlock() {
  HTTP2_DVLOG(3) << "HpackDecoder::StartDecodingBlock, error_detected="
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
  HTTP2_DVLOG(3) << "HpackDecoder::DecodeFragment, error_detected="
                 << (DetectError() ? "true" : "false")
                 << ", size=" << db->Remaining();
  if (DetectError()) {
    HTTP2_CODE_COUNT_N(decompress_failure_3, 3, 23);
    return false;
  }
  // Decode contents of db as an HPACK block fragment, forwards the decoded
  // entries to entry_buffer_, which in turn forwards them to decode_state_,
  // which finally forwards them to the HpackDecoderListener.
  DecodeStatus status = block_decoder_.Decode(db);
  if (status == DecodeStatus::kDecodeError) {
    ReportError(block_decoder_.error());
    HTTP2_CODE_COUNT_N(decompress_failure_3, 4, 23);
    return false;
  } else if (DetectError()) {
    HTTP2_CODE_COUNT_N(decompress_failure_3, 5, 23);
    return false;
  }
  // Should be positioned between entries iff decoding is complete.
  DCHECK_EQ(block_decoder_.before_entry(), status == DecodeStatus::kDecodeDone)
      << status;
  if (!block_decoder_.before_entry()) {
    entry_buffer_.BufferStringsIfUnbuffered();
  }
  return true;
}

bool HpackDecoder::EndDecodingBlock() {
  HTTP2_DVLOG(3) << "HpackDecoder::EndDecodingBlock, error_detected="
                 << (DetectError() ? "true" : "false");
  if (DetectError()) {
    HTTP2_CODE_COUNT_N(decompress_failure_3, 6, 23);
    return false;
  }
  if (!block_decoder_.before_entry()) {
    // The HPACK block ended in the middle of an entry.
    ReportError(HpackDecodingError::kTruncatedBlock);
    HTTP2_CODE_COUNT_N(decompress_failure_3, 7, 23);
    return false;
  }
  decoder_state_.OnHeaderBlockEnd();
  if (DetectError()) {
    // HpackDecoderState will have reported the error.
    HTTP2_CODE_COUNT_N(decompress_failure_3, 8, 23);
    return false;
  }
  return true;
}

bool HpackDecoder::DetectError() {
  if (error_ != HpackDecodingError::kOk) {
    return true;
  }

  if (decoder_state_.error() != HpackDecodingError::kOk) {
    HTTP2_DVLOG(2) << "Error detected in decoder_state_";
    HTTP2_CODE_COUNT_N(decompress_failure_3, 10, 23);
    HTTP2_CODE_COUNT_N(http2_skip_querying_entry_buffer_error, 1, 3);
    error_ = decoder_state_.error();
  } else if (entry_buffer_.error_detected()) {
    // This should never happen, because if an error had occured in
    // |entry_buffer_|, it would have notified its listener, |decoder_state_|.
    if (http2_skip_querying_entry_buffer_error_) {
      HTTP2_CODE_COUNT_N(http2_skip_querying_entry_buffer_error, 2, 3);
    } else {
      HTTP2_DVLOG(2) << "Error detected in entry_buffer_";
      HTTP2_CODE_COUNT_N(decompress_failure_3, 9, 23);
      HTTP2_CODE_COUNT_N(http2_skip_querying_entry_buffer_error, 3, 3);
      // Since this code path should never be executed, error code does not
      // matter as long as it is not HpackDecodingError::kOk.
      error_ = HpackDecodingError::kIndexVarintError;
    }
  }

  return error_ != HpackDecodingError::kOk;
}

size_t HpackDecoder::EstimateMemoryUsage() const {
  return Http2EstimateMemoryUsage(entry_buffer_);
}

void HpackDecoder::ReportError(HpackDecodingError error) {
  HTTP2_DVLOG(3) << "HpackDecoder::ReportError is new="
                 << (error_ == HpackDecodingError::kOk ? "true" : "false")
                 << ", error: " << HpackDecodingErrorToString(error);
  if (error_ == HpackDecodingError::kOk) {
    error_ = error;
    decoder_state_.listener()->OnHeaderErrorDetected(
        HpackDecodingErrorToString(error));
  }
}

}  // namespace http2
