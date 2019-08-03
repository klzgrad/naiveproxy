// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/third_party/quiche/src/http2/hpack/decoder/hpack_entry_decoder_listener.h"

#include "net/third_party/quiche/src/http2/platform/api/http2_logging.h"

namespace http2 {

void HpackEntryDecoderVLoggingListener::OnIndexedHeader(size_t index) {
  HTTP2_VLOG(1) << "OnIndexedHeader, index=" << index;
  if (wrapped_) {
    wrapped_->OnIndexedHeader(index);
  }
}

void HpackEntryDecoderVLoggingListener::OnStartLiteralHeader(
    HpackEntryType entry_type,
    size_t maybe_name_index) {
  HTTP2_VLOG(1) << "OnStartLiteralHeader: entry_type=" << entry_type
                << ", maybe_name_index=" << maybe_name_index;
  if (wrapped_) {
    wrapped_->OnStartLiteralHeader(entry_type, maybe_name_index);
  }
}

void HpackEntryDecoderVLoggingListener::OnNameStart(bool huffman_encoded,
                                                    size_t len) {
  HTTP2_VLOG(1) << "OnNameStart: H=" << huffman_encoded << ", len=" << len;
  if (wrapped_) {
    wrapped_->OnNameStart(huffman_encoded, len);
  }
}

void HpackEntryDecoderVLoggingListener::OnNameData(const char* data,
                                                   size_t len) {
  HTTP2_VLOG(1) << "OnNameData: len=" << len;
  if (wrapped_) {
    wrapped_->OnNameData(data, len);
  }
}

void HpackEntryDecoderVLoggingListener::OnNameEnd() {
  HTTP2_VLOG(1) << "OnNameEnd";
  if (wrapped_) {
    wrapped_->OnNameEnd();
  }
}

void HpackEntryDecoderVLoggingListener::OnValueStart(bool huffman_encoded,
                                                     size_t len) {
  HTTP2_VLOG(1) << "OnValueStart: H=" << huffman_encoded << ", len=" << len;
  if (wrapped_) {
    wrapped_->OnValueStart(huffman_encoded, len);
  }
}

void HpackEntryDecoderVLoggingListener::OnValueData(const char* data,
                                                    size_t len) {
  HTTP2_VLOG(1) << "OnValueData: len=" << len;
  if (wrapped_) {
    wrapped_->OnValueData(data, len);
  }
}

void HpackEntryDecoderVLoggingListener::OnValueEnd() {
  HTTP2_VLOG(1) << "OnValueEnd";
  if (wrapped_) {
    wrapped_->OnValueEnd();
  }
}

void HpackEntryDecoderVLoggingListener::OnDynamicTableSizeUpdate(size_t size) {
  HTTP2_VLOG(1) << "OnDynamicTableSizeUpdate: size=" << size;
  if (wrapped_) {
    wrapped_->OnDynamicTableSizeUpdate(size);
  }
}

}  // namespace http2
