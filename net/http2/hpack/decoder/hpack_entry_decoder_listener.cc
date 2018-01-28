// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/http2/hpack/decoder/hpack_entry_decoder_listener.h"

#include "base/logging.h"

namespace net {

void HpackEntryDecoderVLoggingListener::OnIndexedHeader(size_t index) {
  VLOG(1) << "OnIndexedHeader, index=" << index;
  if (wrapped_) {
    wrapped_->OnIndexedHeader(index);
  }
}

void HpackEntryDecoderVLoggingListener::OnStartLiteralHeader(
    HpackEntryType entry_type,
    size_t maybe_name_index) {
  VLOG(1) << "OnStartLiteralHeader: entry_type=" << entry_type
          << ", maybe_name_index=" << maybe_name_index;
  if (wrapped_) {
    wrapped_->OnStartLiteralHeader(entry_type, maybe_name_index);
  }
}

void HpackEntryDecoderVLoggingListener::OnNameStart(bool huffman_encoded,
                                                    size_t len) {
  VLOG(1) << "OnNameStart: H=" << huffman_encoded << ", len=" << len;
  if (wrapped_) {
    wrapped_->OnNameStart(huffman_encoded, len);
  }
}

void HpackEntryDecoderVLoggingListener::OnNameData(const char* data,
                                                   size_t len) {
  VLOG(1) << "OnNameData: len=" << len;
  if (wrapped_) {
    wrapped_->OnNameData(data, len);
  }
}

void HpackEntryDecoderVLoggingListener::OnNameEnd() {
  VLOG(1) << "OnNameEnd";
  if (wrapped_) {
    wrapped_->OnNameEnd();
  }
}

void HpackEntryDecoderVLoggingListener::OnValueStart(bool huffman_encoded,
                                                     size_t len) {
  VLOG(1) << "OnValueStart: H=" << huffman_encoded << ", len=" << len;
  if (wrapped_) {
    wrapped_->OnValueStart(huffman_encoded, len);
  }
}

void HpackEntryDecoderVLoggingListener::OnValueData(const char* data,
                                                    size_t len) {
  VLOG(1) << "OnValueData: len=" << len;
  if (wrapped_) {
    wrapped_->OnValueData(data, len);
  }
}

void HpackEntryDecoderVLoggingListener::OnValueEnd() {
  VLOG(1) << "OnValueEnd";
  if (wrapped_) {
    wrapped_->OnValueEnd();
  }
}

void HpackEntryDecoderVLoggingListener::OnDynamicTableSizeUpdate(size_t size) {
  VLOG(1) << "OnDynamicTableSizeUpdate: size=" << size;
  if (wrapped_) {
    wrapped_->OnDynamicTableSizeUpdate(size);
  }
}

}  // namespace net
