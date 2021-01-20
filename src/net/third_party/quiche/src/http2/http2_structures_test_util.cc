// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/third_party/quiche/src/http2/http2_structures_test_util.h"

#include <cstdint>

#include "net/third_party/quiche/src/http2/http2_constants.h"
#include "net/third_party/quiche/src/http2/http2_constants_test_util.h"
#include "net/third_party/quiche/src/http2/http2_structures.h"
#include "net/third_party/quiche/src/http2/test_tools/http2_random.h"

namespace http2 {
namespace test {

void Randomize(Http2FrameHeader* p, Http2Random* rng) {
  p->payload_length = rng->Rand32() & 0xffffff;
  p->type = static_cast<Http2FrameType>(rng->Rand8());
  p->flags = static_cast<Http2FrameFlag>(rng->Rand8());
  p->stream_id = rng->Rand32() & StreamIdMask();
}
void Randomize(Http2PriorityFields* p, Http2Random* rng) {
  p->stream_dependency = rng->Rand32() & StreamIdMask();
  p->weight = rng->Rand8() + 1;
  p->is_exclusive = rng->OneIn(2);
}
void Randomize(Http2RstStreamFields* p, Http2Random* rng) {
  p->error_code = static_cast<Http2ErrorCode>(rng->Rand32());
}
void Randomize(Http2SettingFields* p, Http2Random* rng) {
  p->parameter = static_cast<Http2SettingsParameter>(rng->Rand16());
  p->value = rng->Rand32();
}
void Randomize(Http2PushPromiseFields* p, Http2Random* rng) {
  p->promised_stream_id = rng->Rand32() & StreamIdMask();
}
void Randomize(Http2PingFields* p, Http2Random* rng) {
  for (int ndx = 0; ndx < 8; ++ndx) {
    p->opaque_bytes[ndx] = rng->Rand8();
  }
}
void Randomize(Http2GoAwayFields* p, Http2Random* rng) {
  p->last_stream_id = rng->Rand32() & StreamIdMask();
  p->error_code = static_cast<Http2ErrorCode>(rng->Rand32());
}
void Randomize(Http2WindowUpdateFields* p, Http2Random* rng) {
  p->window_size_increment = rng->Rand32() & 0x7fffffff;
}
void Randomize(Http2AltSvcFields* p, Http2Random* rng) {
  p->origin_length = rng->Rand16();
}

void ScrubFlagsOfHeader(Http2FrameHeader* header) {
  uint8_t invalid_mask = InvalidFlagMaskForFrameType(header->type);
  uint8_t keep_mask = ~invalid_mask;
  header->RetainFlags(keep_mask);
}

bool FrameIsPadded(const Http2FrameHeader& header) {
  switch (header.type) {
    case Http2FrameType::DATA:
    case Http2FrameType::HEADERS:
    case Http2FrameType::PUSH_PROMISE:
      return header.IsPadded();
    default:
      return false;
  }
}

bool FrameHasPriority(const Http2FrameHeader& header) {
  switch (header.type) {
    case Http2FrameType::HEADERS:
      return header.HasPriority();
    case Http2FrameType::PRIORITY:
      return true;
    default:
      return false;
  }
}

bool FrameCanHavePayload(const Http2FrameHeader& header) {
  switch (header.type) {
    case Http2FrameType::DATA:
    case Http2FrameType::HEADERS:
    case Http2FrameType::PUSH_PROMISE:
    case Http2FrameType::CONTINUATION:
    case Http2FrameType::PING:
    case Http2FrameType::GOAWAY:
    case Http2FrameType::ALTSVC:
      return true;
    default:
      return false;
  }
}

bool FrameCanHaveHpackPayload(const Http2FrameHeader& header) {
  switch (header.type) {
    case Http2FrameType::HEADERS:
    case Http2FrameType::PUSH_PROMISE:
    case Http2FrameType::CONTINUATION:
      return true;
    default:
      return false;
  }
}

}  // namespace test
}  // namespace http2
