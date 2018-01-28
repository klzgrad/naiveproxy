// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/http2/hpack/decoder/hpack_decoder_listener.h"

namespace net {

HpackDecoderListener::HpackDecoderListener() {}
HpackDecoderListener::~HpackDecoderListener() {}

HpackDecoderNoOpListener::HpackDecoderNoOpListener() {}
HpackDecoderNoOpListener::~HpackDecoderNoOpListener() {}

void HpackDecoderNoOpListener::OnHeaderListStart() {}
void HpackDecoderNoOpListener::OnHeader(HpackEntryType entry_type,
                                        const HpackString& name,
                                        const HpackString& value) {}
void HpackDecoderNoOpListener::OnHeaderListEnd() {}
void HpackDecoderNoOpListener::OnHeaderErrorDetected(
    Http2StringPiece error_message) {}

// static
HpackDecoderNoOpListener* HpackDecoderNoOpListener::NoOpListener() {
  static HpackDecoderNoOpListener* static_instance =
      new HpackDecoderNoOpListener();
  return static_instance;
}

}  // namespace net
