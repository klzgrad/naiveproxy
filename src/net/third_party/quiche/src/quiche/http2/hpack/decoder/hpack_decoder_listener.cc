// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "quiche/http2/hpack/decoder/hpack_decoder_listener.h"

namespace http2 {

HpackDecoderListener::HpackDecoderListener() = default;
HpackDecoderListener::~HpackDecoderListener() = default;

HpackDecoderNoOpListener::HpackDecoderNoOpListener() = default;
HpackDecoderNoOpListener::~HpackDecoderNoOpListener() = default;

void HpackDecoderNoOpListener::OnHeaderListStart() {}
void HpackDecoderNoOpListener::OnHeader(absl::string_view /*name*/,
                                        absl::string_view /*value*/) {}
void HpackDecoderNoOpListener::OnHeaderListEnd() {}
void HpackDecoderNoOpListener::OnHeaderErrorDetected(
    absl::string_view /*error_message*/) {}

// static
HpackDecoderNoOpListener* HpackDecoderNoOpListener::NoOpListener() {
  static HpackDecoderNoOpListener* static_instance =
      new HpackDecoderNoOpListener();
  return static_instance;
}

}  // namespace http2
