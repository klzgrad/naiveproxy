// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "quiche/http2/hpack/decoder/hpack_whole_entry_buffer.h"

#include "absl/strings/str_cat.h"
#include "quiche/common/platform/api/quiche_flag_utils.h"
#include "quiche/common/platform/api/quiche_logging.h"
#include "quiche/common/quiche_text_utils.h"

namespace http2 {

HpackWholeEntryBuffer::HpackWholeEntryBuffer(HpackWholeEntryListener* listener,
                                             size_t max_string_size_bytes)
    : max_string_size_bytes_(max_string_size_bytes) {
  set_listener(listener);
}
HpackWholeEntryBuffer::~HpackWholeEntryBuffer() = default;

void HpackWholeEntryBuffer::set_listener(HpackWholeEntryListener* listener) {
  QUICHE_CHECK(listener);
  listener_ = listener;
}

void HpackWholeEntryBuffer::set_max_string_size_bytes(
    size_t max_string_size_bytes) {
  max_string_size_bytes_ = max_string_size_bytes;
}

void HpackWholeEntryBuffer::BufferStringsIfUnbuffered() {
  name_.BufferStringIfUnbuffered();
  value_.BufferStringIfUnbuffered();
}

void HpackWholeEntryBuffer::OnIndexedHeader(size_t index) {
  QUICHE_DVLOG(2) << "HpackWholeEntryBuffer::OnIndexedHeader: index=" << index;
  listener_->OnIndexedHeader(index);
}

void HpackWholeEntryBuffer::OnStartLiteralHeader(HpackEntryType entry_type,
                                                 size_t maybe_name_index) {
  QUICHE_DVLOG(2) << "HpackWholeEntryBuffer::OnStartLiteralHeader: entry_type="
                  << entry_type << ",  maybe_name_index=" << maybe_name_index;
  entry_type_ = entry_type;
  maybe_name_index_ = maybe_name_index;
}

void HpackWholeEntryBuffer::OnNameStart(bool huffman_encoded, size_t len) {
  QUICHE_DVLOG(2) << "HpackWholeEntryBuffer::OnNameStart: huffman_encoded="
                  << (huffman_encoded ? "true" : "false") << ",  len=" << len;
  QUICHE_DCHECK_EQ(maybe_name_index_, 0u);
  if (!error_detected_) {
    if (len > max_string_size_bytes_) {
      QUICHE_DVLOG(1) << "Name length (" << len
                      << ") is longer than permitted ("
                      << max_string_size_bytes_ << ")";
      ReportError(HpackDecodingError::kNameTooLong);
      QUICHE_CODE_COUNT_N(decompress_failure_3, 18, 23);
      return;
    }
    name_.OnStart(huffman_encoded, len);
  }
}

void HpackWholeEntryBuffer::OnNameData(const char* data, size_t len) {
  QUICHE_DVLOG(2) << "HpackWholeEntryBuffer::OnNameData: len=" << len
                  << " data:\n"
                  << quiche::QuicheTextUtils::HexDump(
                         absl::string_view(data, len));
  QUICHE_DCHECK_EQ(maybe_name_index_, 0u);
  if (!error_detected_ && !name_.OnData(data, len)) {
    ReportError(HpackDecodingError::kNameHuffmanError);
    QUICHE_CODE_COUNT_N(decompress_failure_3, 19, 23);
  }
}

void HpackWholeEntryBuffer::OnNameEnd() {
  QUICHE_DVLOG(2) << "HpackWholeEntryBuffer::OnNameEnd";
  QUICHE_DCHECK_EQ(maybe_name_index_, 0u);
  if (!error_detected_ && !name_.OnEnd()) {
    ReportError(HpackDecodingError::kNameHuffmanError);
    QUICHE_CODE_COUNT_N(decompress_failure_3, 20, 23);
  }
}

void HpackWholeEntryBuffer::OnValueStart(bool huffman_encoded, size_t len) {
  QUICHE_DVLOG(2) << "HpackWholeEntryBuffer::OnValueStart: huffman_encoded="
                  << (huffman_encoded ? "true" : "false") << ",  len=" << len;
  if (!error_detected_) {
    if (len > max_string_size_bytes_) {
      QUICHE_DVLOG(1) << "Value length (" << len << ") of ["
                      << name_.GetStringIfComplete()
                      << "] is longer than permitted ("
                      << max_string_size_bytes_ << ")";

      ReportError(HpackDecodingError::kValueTooLong);
      QUICHE_CODE_COUNT_N(decompress_failure_3, 21, 23);
      return;
    }
    value_.OnStart(huffman_encoded, len);
  }
}

void HpackWholeEntryBuffer::OnValueData(const char* data, size_t len) {
  QUICHE_DVLOG(2) << "HpackWholeEntryBuffer::OnValueData: len=" << len
                  << " data:\n"
                  << quiche::QuicheTextUtils::HexDump(
                         absl::string_view(data, len));
  if (!error_detected_ && !value_.OnData(data, len)) {
    ReportError(HpackDecodingError::kValueHuffmanError);
    QUICHE_CODE_COUNT_N(decompress_failure_3, 22, 23);
  }
}

void HpackWholeEntryBuffer::OnValueEnd() {
  QUICHE_DVLOG(2) << "HpackWholeEntryBuffer::OnValueEnd";
  if (error_detected_) {
    return;
  }
  if (!value_.OnEnd()) {
    ReportError(HpackDecodingError::kValueHuffmanError);
    QUICHE_CODE_COUNT_N(decompress_failure_3, 23, 23);
    return;
  }
  if (maybe_name_index_ == 0) {
    listener_->OnLiteralNameAndValue(entry_type_, &name_, &value_);
    name_.Reset();
  } else {
    listener_->OnNameIndexAndLiteralValue(entry_type_, maybe_name_index_,
                                          &value_);
  }
  value_.Reset();
}

void HpackWholeEntryBuffer::OnDynamicTableSizeUpdate(size_t size) {
  QUICHE_DVLOG(2) << "HpackWholeEntryBuffer::OnDynamicTableSizeUpdate: size="
                  << size;
  listener_->OnDynamicTableSizeUpdate(size);
}

void HpackWholeEntryBuffer::ReportError(HpackDecodingError error) {
  if (!error_detected_) {
    QUICHE_DVLOG(1) << "HpackWholeEntryBuffer::ReportError: "
                    << HpackDecodingErrorToString(error);
    error_detected_ = true;
    listener_->OnHpackDecodeError(error);
    listener_ = HpackWholeEntryNoOpListener::NoOpListener();
  }
}

}  // namespace http2
