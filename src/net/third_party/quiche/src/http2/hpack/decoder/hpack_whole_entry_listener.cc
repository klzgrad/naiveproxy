// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/third_party/quiche/src/http2/hpack/decoder/hpack_whole_entry_listener.h"

namespace http2 {

HpackWholeEntryListener::~HpackWholeEntryListener() = default;

HpackWholeEntryNoOpListener::~HpackWholeEntryNoOpListener() = default;

void HpackWholeEntryNoOpListener::OnIndexedHeader(size_t /*index*/) {}
void HpackWholeEntryNoOpListener::OnNameIndexAndLiteralValue(
    HpackEntryType /*entry_type*/,
    size_t /*name_index*/,
    HpackDecoderStringBuffer* /*value_buffer*/) {}
void HpackWholeEntryNoOpListener::OnLiteralNameAndValue(
    HpackEntryType /*entry_type*/,
    HpackDecoderStringBuffer* /*name_buffer*/,
    HpackDecoderStringBuffer* /*value_buffer*/) {}
void HpackWholeEntryNoOpListener::OnDynamicTableSizeUpdate(size_t /*size*/) {}
void HpackWholeEntryNoOpListener::OnHpackDecodeError(
    HpackDecodingError /*error*/) {}

// static
HpackWholeEntryNoOpListener* HpackWholeEntryNoOpListener::NoOpListener() {
  static HpackWholeEntryNoOpListener* static_instance =
      new HpackWholeEntryNoOpListener();
  return static_instance;
}

}  // namespace http2
