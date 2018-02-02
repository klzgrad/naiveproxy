// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef NET_QUIC_HTTP_QUIC_HTTP_STRUCTURES_TEST_UTIL_H_
#define NET_QUIC_HTTP_QUIC_HTTP_STRUCTURES_TEST_UTIL_H_

#include "net/quic/http/quic_http_structures.h"
#include "net/quic/http/tools/quic_http_frame_builder.h"
#include "net/quic/platform/api/quic_string.h"
#include "net/quic/platform/api/quic_test_random.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace net {
namespace test {

template <class S>
QuicString SerializeStructure(const S& s) {
  QuicHttpFrameBuilder fb;
  fb.Append(s);
  EXPECT_EQ(S::EncodedSize(), fb.size());
  return fb.buffer();
}

// Randomize the members of out, in a manner that yields encodeable contents
// (e.g. a "uint24" field has only the low 24 bits set).
void Randomize(QuicHttpFrameHeader* p, QuicTestRandomBase* rng);
void Randomize(QuicHttpPriorityFields* p, QuicTestRandomBase* rng);
void Randomize(QuicHttpRstStreamFields* p, QuicTestRandomBase* rng);
void Randomize(QuicHttpSettingFields* p, QuicTestRandomBase* rng);
void Randomize(QuicHttpPushPromiseFields* p, QuicTestRandomBase* rng);
void Randomize(QuicHttpPingFields* p, QuicTestRandomBase* rng);
void Randomize(QuicHttpGoAwayFields* p, QuicTestRandomBase* rng);
void Randomize(QuicHttpWindowUpdateFields* p, QuicTestRandomBase* rng);
void Randomize(QuicHttpAltSvcFields* p, QuicTestRandomBase* rng);

// Clear bits of header->flags that are known to be invalid for the
// type. For unknown frame types, no change is made.
void ScrubFlagsOfHeader(QuicHttpFrameHeader* header);

// Is the frame with this header padded? Only true for known/supported frame
// types.
bool FrameIsPadded(const QuicHttpFrameHeader& header);

// Does the frame with this header have QuicHttpPriorityFields?
bool FrameHasPriority(const QuicHttpFrameHeader& header);

// Does the frame with this header have a variable length payload (including
// empty) payload (e.g. DATA or HEADERS)? Really a test of the frame type.
bool FrameCanHavePayload(const QuicHttpFrameHeader& header);

// Does the frame with this header have a variable length HPQUIC_HTTP_ACK
// payload (including empty) payload (e.g. HEADERS)? Really a test of the frame
// type.
bool FrameCanHaveHpackPayload(const QuicHttpFrameHeader& header);

}  // namespace test
}  // namespace net

#endif  // NET_QUIC_HTTP_QUIC_HTTP_STRUCTURES_TEST_UTIL_H_
