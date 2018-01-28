// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/quic/http/quic_http_structures_test_util.h"

#include <cstdint>

#include "net/quic/http/quic_http_constants.h"
#include "net/quic/http/quic_http_constants_test_util.h"
#include "net/quic/http/quic_http_structures.h"
#include "net/quic/platform/api/quic_test_random.h"

namespace net {
namespace test {

void Randomize(QuicHttpFrameHeader* p, QuicTestRandomBase* rng) {
  p->payload_length = rng->Rand32() & 0xffffff;
  p->type = static_cast<QuicHttpFrameType>(rng->Rand8());
  p->flags = static_cast<QuicHttpFrameFlag>(rng->Rand8());
  p->stream_id = rng->Rand32() & QuicHttpStreamIdMask();
}
void Randomize(QuicHttpPriorityFields* p, QuicTestRandomBase* rng) {
  p->stream_dependency = rng->Rand32() & QuicHttpStreamIdMask();
  p->weight = rng->Rand8() + 1;
  p->is_exclusive = rng->OneIn(2);
}
void Randomize(QuicHttpRstStreamFields* p, QuicTestRandomBase* rng) {
  p->error_code = static_cast<QuicHttpErrorCode>(rng->Rand32());
}
void Randomize(QuicHttpSettingFields* p, QuicTestRandomBase* rng) {
  p->parameter = static_cast<QuicHttpSettingsParameter>(rng->Rand16());
  p->value = rng->Rand32();
}
void Randomize(QuicHttpPushPromiseFields* p, QuicTestRandomBase* rng) {
  p->promised_stream_id = rng->Rand32() & QuicHttpStreamIdMask();
}
void Randomize(QuicHttpPingFields* p, QuicTestRandomBase* rng) {
  for (int ndx = 0; ndx < 8; ++ndx) {
    p->opaque_bytes[ndx] = rng->Rand8();
  }
}
void Randomize(QuicHttpGoAwayFields* p, QuicTestRandomBase* rng) {
  p->last_stream_id = rng->Rand32() & QuicHttpStreamIdMask();
  p->error_code = static_cast<QuicHttpErrorCode>(rng->Rand32());
}
void Randomize(QuicHttpWindowUpdateFields* p, QuicTestRandomBase* rng) {
  p->window_size_increment = rng->Rand32() & 0x7fffffff;
}
void Randomize(QuicHttpAltSvcFields* p, QuicTestRandomBase* rng) {
  p->origin_length = rng->Rand16();
}

void ScrubFlagsOfHeader(QuicHttpFrameHeader* header) {
  uint8_t invalid_mask = InvalidFlagMaskForFrameType(header->type);
  uint8_t keep_mask = ~invalid_mask;
  header->RetainFlags(keep_mask);
}

bool FrameIsPadded(const QuicHttpFrameHeader& header) {
  switch (header.type) {
    case QuicHttpFrameType::DATA:
    case QuicHttpFrameType::HEADERS:
    case QuicHttpFrameType::PUSH_PROMISE:
      return header.IsPadded();
    default:
      return false;
  }
}

bool FrameHasPriority(const QuicHttpFrameHeader& header) {
  switch (header.type) {
    case QuicHttpFrameType::HEADERS:
      return header.HasPriority();
    case QuicHttpFrameType::QUIC_HTTP_PRIORITY:
      return true;
    default:
      return false;
  }
}

bool FrameCanHavePayload(const QuicHttpFrameHeader& header) {
  switch (header.type) {
    case QuicHttpFrameType::DATA:
    case QuicHttpFrameType::HEADERS:
    case QuicHttpFrameType::PUSH_PROMISE:
    case QuicHttpFrameType::CONTINUATION:
    case QuicHttpFrameType::PING:
    case QuicHttpFrameType::GOAWAY:
    case QuicHttpFrameType::ALTSVC:
      return true;
    default:
      return false;
  }
}

bool FrameCanHaveHpackPayload(const QuicHttpFrameHeader& header) {
  switch (header.type) {
    case QuicHttpFrameType::HEADERS:
    case QuicHttpFrameType::PUSH_PROMISE:
    case QuicHttpFrameType::CONTINUATION:
      return true;
    default:
      return false;
  }
}

}  // namespace test
}  // namespace net
