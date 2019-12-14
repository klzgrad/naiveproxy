// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef QUICHE_HTTP2_HTTP2_STRUCTURES_TEST_UTIL_H_
#define QUICHE_HTTP2_HTTP2_STRUCTURES_TEST_UTIL_H_

#include <string>

#include "testing/gtest/include/gtest/gtest.h"
#include "net/third_party/quiche/src/http2/http2_structures.h"
#include "net/third_party/quiche/src/http2/test_tools/http2_random.h"
#include "net/third_party/quiche/src/http2/tools/http2_frame_builder.h"

namespace http2 {
namespace test {

template <class S>
std::string SerializeStructure(const S& s) {
  Http2FrameBuilder fb;
  fb.Append(s);
  EXPECT_EQ(S::EncodedSize(), fb.size());
  return fb.buffer();
}

// Randomize the members of out, in a manner that yields encodeable contents
// (e.g. a "uint24" field has only the low 24 bits set).
void Randomize(Http2FrameHeader* out, Http2Random* rng);
void Randomize(Http2PriorityFields* out, Http2Random* rng);
void Randomize(Http2RstStreamFields* out, Http2Random* rng);
void Randomize(Http2SettingFields* out, Http2Random* rng);
void Randomize(Http2PushPromiseFields* out, Http2Random* rng);
void Randomize(Http2PingFields* out, Http2Random* rng);
void Randomize(Http2GoAwayFields* out, Http2Random* rng);
void Randomize(Http2WindowUpdateFields* out, Http2Random* rng);
void Randomize(Http2AltSvcFields* out, Http2Random* rng);

// Clear bits of header->flags that are known to be invalid for the
// type. For unknown frame types, no change is made.
void ScrubFlagsOfHeader(Http2FrameHeader* header);

// Is the frame with this header padded? Only true for known/supported frame
// types.
bool FrameIsPadded(const Http2FrameHeader& header);

// Does the frame with this header have Http2PriorityFields?
bool FrameHasPriority(const Http2FrameHeader& header);

// Does the frame with this header have a variable length payload (including
// empty) payload (e.g. DATA or HEADERS)? Really a test of the frame type.
bool FrameCanHavePayload(const Http2FrameHeader& header);

// Does the frame with this header have a variable length HPACK payload
// (including empty) payload (e.g. HEADERS)? Really a test of the frame type.
bool FrameCanHaveHpackPayload(const Http2FrameHeader& header);

}  // namespace test
}  // namespace http2

#endif  // QUICHE_HTTP2_HTTP2_STRUCTURES_TEST_UTIL_H_
