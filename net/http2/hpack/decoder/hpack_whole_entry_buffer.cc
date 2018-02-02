// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/http2/hpack/decoder/hpack_whole_entry_buffer.h"

#include "base/logging.h"
#include "base/trace_event/memory_usage_estimator.h"
#include "net/http2/platform/api/http2_string_utils.h"

namespace net {

HpackWholeEntryBuffer::HpackWholeEntryBuffer(HpackWholeEntryListener* listener,
                                             size_t max_string_size_bytes)
    : max_string_size_bytes_(max_string_size_bytes) {
  set_listener(listener);
}
HpackWholeEntryBuffer::~HpackWholeEntryBuffer() {}

void HpackWholeEntryBuffer::set_listener(HpackWholeEntryListener* listener) {
  CHECK(listener);
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

size_t HpackWholeEntryBuffer::EstimateMemoryUsage() const {
  return base::trace_event::EstimateMemoryUsage(name_) +
         base::trace_event::EstimateMemoryUsage(value_);
}

void HpackWholeEntryBuffer::OnIndexedHeader(size_t index) {
  DVLOG(2) << "HpackWholeEntryBuffer::OnIndexedHeader: index=" << index;
  listener_->OnIndexedHeader(index);
}

void HpackWholeEntryBuffer::OnStartLiteralHeader(HpackEntryType entry_type,
                                                 size_t maybe_name_index) {
  DVLOG(2) << "HpackWholeEntryBuffer::OnStartLiteralHeader: entry_type="
           << entry_type << ",  maybe_name_index=" << maybe_name_index;
  entry_type_ = entry_type;
  maybe_name_index_ = maybe_name_index;
}

void HpackWholeEntryBuffer::OnNameStart(bool huffman_encoded, size_t len) {
  DVLOG(2) << "HpackWholeEntryBuffer::OnNameStart: huffman_encoded="
           << (huffman_encoded ? "true" : "false") << ",  len=" << len;
  DCHECK_EQ(maybe_name_index_, 0u);
  if (!error_detected_) {
    if (len > max_string_size_bytes_) {
      DVLOG(1) << "Name length (" << len << ") is longer than permitted ("
               << max_string_size_bytes_ << ")";
      ReportError("HPACK entry name size is too long.");
      return;
    }
    name_.OnStart(huffman_encoded, len);
  }
}

void HpackWholeEntryBuffer::OnNameData(const char* data, size_t len) {
  DVLOG(2) << "HpackWholeEntryBuffer::OnNameData: len=" << len << " data:\n"
           << Http2HexDump(Http2StringPiece(data, len));
  DCHECK_EQ(maybe_name_index_, 0u);
  if (!error_detected_ && !name_.OnData(data, len)) {
    ReportError("Error decoding HPACK entry name.");
  }
}

void HpackWholeEntryBuffer::OnNameEnd() {
  DVLOG(2) << "HpackWholeEntryBuffer::OnNameEnd";
  DCHECK_EQ(maybe_name_index_, 0u);
  if (!error_detected_ && !name_.OnEnd()) {
    ReportError("Error decoding HPACK entry name.");
  }
}

void HpackWholeEntryBuffer::OnValueStart(bool huffman_encoded, size_t len) {
  DVLOG(2) << "HpackWholeEntryBuffer::OnValueStart: huffman_encoded="
           << (huffman_encoded ? "true" : "false") << ",  len=" << len;
  if (!error_detected_) {
    if (len > max_string_size_bytes_) {
      DVLOG(1) << "Value length (" << len << ") is longer than permitted ("
               << max_string_size_bytes_ << ")";
      ReportError("HPACK entry value size is too long.");
      return;
    }
    value_.OnStart(huffman_encoded, len);
  }
}

void HpackWholeEntryBuffer::OnValueData(const char* data, size_t len) {
  DVLOG(2) << "HpackWholeEntryBuffer::OnValueData: len=" << len << " data:\n"
           << Http2HexDump(Http2StringPiece(data, len));
  if (!error_detected_ && !value_.OnData(data, len)) {
    ReportError("Error decoding HPACK entry value.");
  }
}

void HpackWholeEntryBuffer::OnValueEnd() {
  DVLOG(2) << "HpackWholeEntryBuffer::OnValueEnd";
  if (error_detected_) {
    return;
  }
  if (!value_.OnEnd()) {
    ReportError("Error decoding HPACK entry value.");
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
  DVLOG(2) << "HpackWholeEntryBuffer::OnDynamicTableSizeUpdate: size=" << size;
  listener_->OnDynamicTableSizeUpdate(size);
}

void HpackWholeEntryBuffer::ReportError(Http2StringPiece error_message) {
  if (!error_detected_) {
    DVLOG(1) << "HpackWholeEntryBuffer::ReportError: " << error_message;
    error_detected_ = true;
    listener_->OnHpackDecodeError(error_message);
    listener_ = HpackWholeEntryNoOpListener::NoOpListener();
  }
}

}  // namespace net
