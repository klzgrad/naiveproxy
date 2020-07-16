// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/third_party/quiche/src/http2/decoder/http2_structure_decoder_test_util.h"

#include <cstddef>

namespace http2 {
namespace test {

// static
void Http2StructureDecoderPeer::Randomize(Http2StructureDecoder* p,
                                          Http2Random* rng) {
  p->offset_ = rng->Rand32();
  for (size_t i = 0; i < sizeof p->buffer_; ++i) {
    p->buffer_[i] = rng->Rand8();
  }
}

}  // namespace test
}  // namespace http2
