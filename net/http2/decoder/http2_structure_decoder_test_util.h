// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef NET_HTTP2_DECODER_HTTP2_STRUCTURE_DECODER_TEST_UTIL_H_
#define NET_HTTP2_DECODER_HTTP2_STRUCTURE_DECODER_TEST_UTIL_H_

#include "net/http2/decoder/http2_structure_decoder.h"

#include "net/http2/tools/http2_random.h"

namespace net {
namespace test {

class Http2StructureDecoderPeer {
 public:
  // Overwrite the Http2StructureDecoder instance with random values.
  static void Randomize(Http2StructureDecoder* p, RandomBase* rng);
};

}  // namespace test
}  // namespace net

#endif  // NET_HTTP2_DECODER_HTTP2_STRUCTURE_DECODER_TEST_UTIL_H_
