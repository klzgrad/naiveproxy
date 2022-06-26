// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef QUICHE_HTTP2_TEST_TOOLS_HTTP2_STRUCTURE_DECODER_TEST_UTIL_H_
#define QUICHE_HTTP2_TEST_TOOLS_HTTP2_STRUCTURE_DECODER_TEST_UTIL_H_

#include "quiche/http2/decoder/http2_structure_decoder.h"
#include "quiche/http2/test_tools/http2_random.h"
#include "quiche/common/platform/api/quiche_export.h"

namespace http2 {
namespace test {

class QUICHE_NO_EXPORT Http2StructureDecoderPeer {
 public:
  // Overwrite the Http2StructureDecoder instance with random values.
  static void Randomize(Http2StructureDecoder* p, Http2Random* rng);
};

}  // namespace test
}  // namespace http2

#endif  // QUICHE_HTTP2_TEST_TOOLS_HTTP2_STRUCTURE_DECODER_TEST_UTIL_H_
