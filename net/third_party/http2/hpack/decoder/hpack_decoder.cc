// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/third_party/http2/hpack/decoder/hpack_decoder.h"

#include "base/logging.h"
#include "base/trace_event/memory_usage_estimator.h"
#include "net/third_party/http2/decoder/decode_status.h"

namespace http2 {

HpackDecoder::HpackDecoder(HpackDecoderListener* listener,
                           size_t max_string_size)
    : decoder_state_(listener),
      entry_buffer_(&decoder_state_, max_string_size),
      block_decoder_(&entry_buffer_),
      error_detected_(false) {}

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
  DVLOG(3) << "HpackDecoder::StartDecodingBlock, error_detected="
           << (error_detected() ? "true" : "false");
  if (error_detected()) {
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
  DVLOG(3) << "HpackDecoder::DecodeFragment, error_detected="
           << (error_detected() ? "true" : "false")
           << ", size=" << db->Remaining();
  if (error_detected()) {
    return false;
  }
  // Decode contents of db as an HPACK block fragment, forwards the decoded
  // entries to entry_buffer_, which in turn forwards them to decode_state_,
  // which finally forwards them to the HpackDecoderListener.
  DecodeStatus status = block_decoder_.Decode(db);
  if (status == DecodeStatus::kDecodeError) {
    // This has probably already been reported, but just in case...
    ReportError("HPACK block malformed.");
    return false;
  } else if (error_detected()) {
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
  DVLOG(3) << "HpackDecoder::EndDecodingBlock, error_detected="
           << (error_detected() ? "true" : "false");
  if (error_detected()) {
    return false;
  }
  if (!block_decoder_.before_entry()) {
    // The HPACK block ended in the middle of an entry.
    ReportError("HPACK block truncated.");
    return false;
  }
  decoder_state_.OnHeaderBlockEnd();
  if (error_detected()) {
    // HpackDecoderState will have reported the error.
    return false;
  }
  return true;
}

bool HpackDecoder::error_detected() {
  if (!error_detected_) {
    if (entry_buffer_.error_detected()) {
      DVLOG(2) << "HpackDecoder::error_detected in entry_buffer_";
      error_detected_ = true;
    } else if (decoder_state_.error_detected()) {
      DVLOG(2) << "HpackDecoder::error_detected in decoder_state_";
      error_detected_ = true;
    }
  }
  return error_detected_;
}

size_t HpackDecoder::EstimateMemoryUsage() const {
  return base::trace_event::EstimateMemoryUsage(entry_buffer_);
}

void HpackDecoder::ReportError(Http2StringPiece error_message) {
  DVLOG(3) << "HpackDecoder::ReportError is new="
           << (!error_detected_ ? "true" : "false")
           << ", error_message: " << error_message;
  if (!error_detected_) {
    error_detected_ = true;
    decoder_state_.listener()->OnHeaderErrorDetected(error_message);
  }
}

}  // namespace http2
