// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "quiche/http2/hpack/decoder/hpack_string_decoder_listener.h"

#include "quiche/common/platform/api/quiche_logging.h"

namespace http2 {
namespace test {

void HpackStringDecoderVLoggingListener::OnStringStart(bool huffman_encoded,
                                                       size_t len) {
  QUICHE_VLOG(1) << "OnStringStart: H=" << huffman_encoded << ", len=" << len;
  if (wrapped_) {
    wrapped_->OnStringStart(huffman_encoded, len);
  }
}

void HpackStringDecoderVLoggingListener::OnStringData(const char* data,
                                                      size_t len) {
  QUICHE_VLOG(1) << "OnStringData: len=" << len;
  if (wrapped_) {
    return wrapped_->OnStringData(data, len);
  }
}

void HpackStringDecoderVLoggingListener::OnStringEnd() {
  QUICHE_VLOG(1) << "OnStringEnd";
  if (wrapped_) {
    return wrapped_->OnStringEnd();
  }
}

}  // namespace test
}  // namespace http2
