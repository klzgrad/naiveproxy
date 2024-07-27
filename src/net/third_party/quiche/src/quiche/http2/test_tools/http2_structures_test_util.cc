// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "quiche/http2/test_tools/http2_structures_test_util.h"

#include <cstdint>

#include "quiche/http2/http2_constants.h"
#include "quiche/http2/http2_structures.h"
#include "quiche/http2/test_tools/http2_constants_test_util.h"
#include "quiche/http2/test_tools/http2_random.h"

namespace http2 {
namespace test {

void Randomize(Http2FrameHeader* out, Http2Random* rng) {
  out->payload_length = rng->Rand32() & 0xffffff;
  out->type = static_cast<Http2FrameType>(rng->Rand8());
  out->flags = static_cast<Http2FrameFlag>(rng->Rand8());
  out->stream_id = rng->Rand32() & StreamIdMask();
}
void Randomize(Http2PriorityFields* out, Http2Random* rng) {
  out->stream_dependency = rng->Rand32() & StreamIdMask();
  out->weight = rng->Rand8() + 1;
  out->is_exclusive = rng->OneIn(2);
}
void Randomize(Http2RstStreamFields* out, Http2Random* rng) {
  out->error_code = static_cast<Http2ErrorCode>(rng->Rand32());
}
void Randomize(Http2SettingFields* out, Http2Random* rng) {
  out->parameter = static_cast<Http2SettingsParameter>(rng->Rand16());
  out->value = rng->Rand32();
}
void Randomize(Http2PushPromiseFields* out, Http2Random* rng) {
  out->promised_stream_id = rng->Rand32() & StreamIdMask();
}
void Randomize(Http2PingFields* out, Http2Random* rng) {
  for (int ndx = 0; ndx < 8; ++ndx) {
    out->opaque_bytes[ndx] = rng->Rand8();
  }
}
void Randomize(Http2GoAwayFields* out, Http2Random* rng) {
  out->last_stream_id = rng->Rand32() & StreamIdMask();
  out->error_code = static_cast<Http2ErrorCode>(rng->Rand32());
}
void Randomize(Http2WindowUpdateFields* out, Http2Random* rng) {
  out->window_size_increment = rng->Rand32() & 0x7fffffff;
}
void Randomize(Http2AltSvcFields* out, Http2Random* rng) {
  out->origin_length = rng->Rand16();
}
void Randomize(Http2PriorityUpdateFields* out, Http2Random* rng) {
  out->prioritized_stream_id = rng->Rand32() & StreamIdMask();
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
