// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/quic/http/decoder/quic_http_structure_decoder_test_util.h"

namespace net {
namespace test {

void QuicHttpStructureDecoderPeer::Randomize(QuicHttpStructureDecoder* p,
                                             QuicTestRandomBase* rng) {
  p->offset_ = rng->Rand32();
  for (size_t i = 0; i < sizeof p->buffer_; ++i) {
    p->buffer_[i] = rng->Rand8();
  }
}

}  // namespace test
}  // namespace net
