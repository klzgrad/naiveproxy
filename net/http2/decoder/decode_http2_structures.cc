// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/http2/decoder/decode_http2_structures.h"

#include "base/logging.h"
#include "net/http2/decoder/decode_buffer.h"
#include "net/http2/http2_constants.h"

namespace net {

// Http2FrameHeader decoding:

void DoDecode(Http2FrameHeader* out, DecodeBuffer* b) {
  DCHECK_NE(nullptr, out);
  DCHECK_NE(nullptr, b);
  DCHECK_LE(Http2FrameHeader::EncodedSize(), b->Remaining());
  out->payload_length = b->DecodeUInt24();
  out->type = static_cast<Http2FrameType>(b->DecodeUInt8());
  out->flags = static_cast<Http2FrameFlag>(b->DecodeUInt8());
  out->stream_id = b->DecodeUInt31();
}

// Http2PriorityFields decoding:

void DoDecode(Http2PriorityFields* out, DecodeBuffer* b) {
  DCHECK_NE(nullptr, out);
  DCHECK_NE(nullptr, b);
  DCHECK_LE(Http2PriorityFields::EncodedSize(), b->Remaining());
  uint32_t stream_id_and_flag = b->DecodeUInt32();
  out->stream_dependency = stream_id_and_flag & StreamIdMask();
  if (out->stream_dependency == stream_id_and_flag) {
    out->is_exclusive = false;
  } else {
    out->is_exclusive = true;
  }
  // Note that chars are automatically promoted to ints during arithmetic,
  // so 255 + 1 doesn't end up as zero.
  out->weight = b->DecodeUInt8() + 1;
}

// Http2RstStreamFields decoding:

void DoDecode(Http2RstStreamFields* out, DecodeBuffer* b) {
  DCHECK_NE(nullptr, out);
  DCHECK_NE(nullptr, b);
  DCHECK_LE(Http2RstStreamFields::EncodedSize(), b->Remaining());
  out->error_code = static_cast<Http2ErrorCode>(b->DecodeUInt32());
}

// Http2SettingFields decoding:

void DoDecode(Http2SettingFields* out, DecodeBuffer* b) {
  DCHECK_NE(nullptr, out);
  DCHECK_NE(nullptr, b);
  DCHECK_LE(Http2SettingFields::EncodedSize(), b->Remaining());
  out->parameter = static_cast<Http2SettingsParameter>(b->DecodeUInt16());
  out->value = b->DecodeUInt32();
}

// Http2PushPromiseFields decoding:

void DoDecode(Http2PushPromiseFields* out, DecodeBuffer* b) {
  DCHECK_NE(nullptr, out);
  DCHECK_NE(nullptr, b);
  DCHECK_LE(Http2PushPromiseFields::EncodedSize(), b->Remaining());
  out->promised_stream_id = b->DecodeUInt31();
}

// Http2PingFields decoding:

void DoDecode(Http2PingFields* out, DecodeBuffer* b) {
  DCHECK_NE(nullptr, out);
  DCHECK_NE(nullptr, b);
  DCHECK_LE(Http2PingFields::EncodedSize(), b->Remaining());
  memcpy(out->opaque_bytes, b->cursor(), Http2PingFields::EncodedSize());
  b->AdvanceCursor(Http2PingFields::EncodedSize());
}

// Http2GoAwayFields decoding:

void DoDecode(Http2GoAwayFields* out, DecodeBuffer* b) {
  DCHECK_NE(nullptr, out);
  DCHECK_NE(nullptr, b);
  DCHECK_LE(Http2GoAwayFields::EncodedSize(), b->Remaining());
  out->last_stream_id = b->DecodeUInt31();
  out->error_code = static_cast<Http2ErrorCode>(b->DecodeUInt32());
}

// Http2WindowUpdateFields decoding:

void DoDecode(Http2WindowUpdateFields* out, DecodeBuffer* b) {
  DCHECK_NE(nullptr, out);
  DCHECK_NE(nullptr, b);
  DCHECK_LE(Http2WindowUpdateFields::EncodedSize(), b->Remaining());
  out->window_size_increment = b->DecodeUInt31();
}

// Http2AltSvcFields decoding:

void DoDecode(Http2AltSvcFields* out, DecodeBuffer* b) {
  DCHECK_NE(nullptr, out);
  DCHECK_NE(nullptr, b);
  DCHECK_LE(Http2AltSvcFields::EncodedSize(), b->Remaining());
  out->origin_length = b->DecodeUInt16();
}

}  // namespace net
