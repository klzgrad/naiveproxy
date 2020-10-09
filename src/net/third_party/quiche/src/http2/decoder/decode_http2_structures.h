// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef QUICHE_HTTP2_DECODER_DECODE_HTTP2_STRUCTURES_H_
#define QUICHE_HTTP2_DECODER_DECODE_HTTP2_STRUCTURES_H_

// Provides functions for decoding the fixed size structures in the HTTP/2 spec.

#include "net/third_party/quiche/src/http2/decoder/decode_buffer.h"
#include "net/third_party/quiche/src/http2/http2_structures.h"
#include "net/third_party/quiche/src/common/platform/api/quiche_export.h"

namespace http2 {

// DoDecode(STRUCTURE* out, DecodeBuffer* b) decodes the structure from start
// to end, advancing the cursor by STRUCTURE::EncodedSize(). The decode buffer
// must be large enough (i.e. b->Remaining() >= STRUCTURE::EncodedSize()).

QUICHE_EXPORT_PRIVATE void DoDecode(Http2FrameHeader* out, DecodeBuffer* b);
QUICHE_EXPORT_PRIVATE void DoDecode(Http2PriorityFields* out, DecodeBuffer* b);
QUICHE_EXPORT_PRIVATE void DoDecode(Http2RstStreamFields* out, DecodeBuffer* b);
QUICHE_EXPORT_PRIVATE void DoDecode(Http2SettingFields* out, DecodeBuffer* b);
QUICHE_EXPORT_PRIVATE void DoDecode(Http2PushPromiseFields* out,
                                    DecodeBuffer* b);
QUICHE_EXPORT_PRIVATE void DoDecode(Http2PingFields* out, DecodeBuffer* b);
QUICHE_EXPORT_PRIVATE void DoDecode(Http2GoAwayFields* out, DecodeBuffer* b);
QUICHE_EXPORT_PRIVATE void DoDecode(Http2WindowUpdateFields* out,
                                    DecodeBuffer* b);
QUICHE_EXPORT_PRIVATE void DoDecode(Http2AltSvcFields* out, DecodeBuffer* b);

}  // namespace http2

#endif  // QUICHE_HTTP2_DECODER_DECODE_HTTP2_STRUCTURES_H_
