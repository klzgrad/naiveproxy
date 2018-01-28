// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef NET_HTTP2_DECODER_DECODE_HTTP2_STRUCTURES_H_
#define NET_HTTP2_DECODER_DECODE_HTTP2_STRUCTURES_H_

// Provides functions for decoding the fixed size structures in the HTTP/2 spec.

#include "net/http2/decoder/decode_buffer.h"
#include "net/http2/http2_structures.h"
#include "net/http2/platform/api/http2_export.h"

namespace net {

// DoDecode(STRUCTURE* out, DecodeBuffer* b) decodes the structure from start
// to end, advancing the cursor by STRUCTURE::EncodedSize(). The decode buffer
// must be large enough (i.e. b->Remaining() >= STRUCTURE::EncodedSize()).

HTTP2_EXPORT_PRIVATE void DoDecode(Http2FrameHeader* out, DecodeBuffer* b);
HTTP2_EXPORT_PRIVATE void DoDecode(Http2PriorityFields* out, DecodeBuffer* b);
HTTP2_EXPORT_PRIVATE void DoDecode(Http2RstStreamFields* out, DecodeBuffer* b);
HTTP2_EXPORT_PRIVATE void DoDecode(Http2SettingFields* out, DecodeBuffer* b);
HTTP2_EXPORT_PRIVATE void DoDecode(Http2PushPromiseFields* out,
                                   DecodeBuffer* b);
HTTP2_EXPORT_PRIVATE void DoDecode(Http2PingFields* out, DecodeBuffer* b);
HTTP2_EXPORT_PRIVATE void DoDecode(Http2GoAwayFields* out, DecodeBuffer* b);
HTTP2_EXPORT_PRIVATE void DoDecode(Http2WindowUpdateFields* out,
                                   DecodeBuffer* b);
HTTP2_EXPORT_PRIVATE void DoDecode(Http2AltSvcFields* out, DecodeBuffer* b);

}  // namespace net

#endif  // NET_HTTP2_DECODER_DECODE_HTTP2_STRUCTURES_H_
