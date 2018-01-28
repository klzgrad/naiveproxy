// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/quic/http/decoder/quic_http_decode_structures.h"

#include <cstdint>

#include "base/logging.h"
#include "net/quic/http/decoder/quic_http_decode_buffer.h"
#include "net/quic/http/quic_http_constants.h"

namespace net {

// QuicHttpFrameHeader decoding:

void DoDecode(QuicHttpFrameHeader* out, QuicHttpDecodeBuffer* b) {
  DCHECK_NE(nullptr, out);
  DCHECK_NE(nullptr, b);
  DCHECK_LE(QuicHttpFrameHeader::EncodedSize(), b->Remaining());
  out->payload_length = b->DecodeUInt24();
  out->type = static_cast<QuicHttpFrameType>(b->DecodeUInt8());
  out->flags = static_cast<QuicHttpFrameFlag>(b->DecodeUInt8());
  out->stream_id = b->DecodeUInt31();
}

// QuicHttpPriorityFields decoding:

void DoDecode(QuicHttpPriorityFields* out, QuicHttpDecodeBuffer* b) {
  DCHECK_NE(nullptr, out);
  DCHECK_NE(nullptr, b);
  DCHECK_LE(QuicHttpPriorityFields::EncodedSize(), b->Remaining());
  uint32_t stream_id_and_flag = b->DecodeUInt32();
  out->stream_dependency = stream_id_and_flag & QuicHttpStreamIdMask();
  if (out->stream_dependency == stream_id_and_flag) {
    out->is_exclusive = false;
  } else {
    out->is_exclusive = true;
  }
  // Note that chars are automatically promoted to ints during arithmetic,
  // so 255 + 1 doesn't end up as zero.
  out->weight = b->DecodeUInt8() + 1;
}

// QuicHttpRstStreamFields decoding:

void DoDecode(QuicHttpRstStreamFields* out, QuicHttpDecodeBuffer* b) {
  DCHECK_NE(nullptr, out);
  DCHECK_NE(nullptr, b);
  DCHECK_LE(QuicHttpRstStreamFields::EncodedSize(), b->Remaining());
  out->error_code = static_cast<QuicHttpErrorCode>(b->DecodeUInt32());
}

// QuicHttpSettingFields decoding:

void DoDecode(QuicHttpSettingFields* out, QuicHttpDecodeBuffer* b) {
  DCHECK_NE(nullptr, out);
  DCHECK_NE(nullptr, b);
  DCHECK_LE(QuicHttpSettingFields::EncodedSize(), b->Remaining());
  out->parameter = static_cast<QuicHttpSettingsParameter>(b->DecodeUInt16());
  out->value = b->DecodeUInt32();
}

// QuicHttpPushPromiseFields decoding:

void DoDecode(QuicHttpPushPromiseFields* out, QuicHttpDecodeBuffer* b) {
  DCHECK_NE(nullptr, out);
  DCHECK_NE(nullptr, b);
  DCHECK_LE(QuicHttpPushPromiseFields::EncodedSize(), b->Remaining());
  out->promised_stream_id = b->DecodeUInt31();
}

// QuicHttpPingFields decoding:

void DoDecode(QuicHttpPingFields* out, QuicHttpDecodeBuffer* b) {
  DCHECK_NE(nullptr, out);
  DCHECK_NE(nullptr, b);
  DCHECK_LE(QuicHttpPingFields::EncodedSize(), b->Remaining());
  memcpy(out->opaque_bytes, b->cursor(), QuicHttpPingFields::EncodedSize());
  b->AdvanceCursor(QuicHttpPingFields::EncodedSize());
}

// QuicHttpGoAwayFields decoding:

void DoDecode(QuicHttpGoAwayFields* out, QuicHttpDecodeBuffer* b) {
  DCHECK_NE(nullptr, out);
  DCHECK_NE(nullptr, b);
  DCHECK_LE(QuicHttpGoAwayFields::EncodedSize(), b->Remaining());
  out->last_stream_id = b->DecodeUInt31();
  out->error_code = static_cast<QuicHttpErrorCode>(b->DecodeUInt32());
}

// QuicHttpWindowUpdateFields decoding:

void DoDecode(QuicHttpWindowUpdateFields* out, QuicHttpDecodeBuffer* b) {
  DCHECK_NE(nullptr, out);
  DCHECK_NE(nullptr, b);
  DCHECK_LE(QuicHttpWindowUpdateFields::EncodedSize(), b->Remaining());
  out->window_size_increment = b->DecodeUInt31();
}

// QuicHttpAltSvcFields decoding:

void DoDecode(QuicHttpAltSvcFields* out, QuicHttpDecodeBuffer* b) {
  DCHECK_NE(nullptr, out);
  DCHECK_NE(nullptr, b);
  DCHECK_LE(QuicHttpAltSvcFields::EncodedSize(), b->Remaining());
  out->origin_length = b->DecodeUInt16();
}

}  // namespace net
