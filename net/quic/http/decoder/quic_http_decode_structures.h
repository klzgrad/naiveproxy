// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef NET_QUIC_HTTP_DECODER_QUIC_HTTP_DECODE_STRUCTURES_H_
#define NET_QUIC_HTTP_DECODER_QUIC_HTTP_DECODE_STRUCTURES_H_

// Provides functions for decoding the fixed size structures in the HTTP/2 spec.

#include "net/quic/http/decoder/quic_http_decode_buffer.h"
#include "net/quic/http/quic_http_structures.h"
#include "net/quic/platform/api/quic_export.h"

namespace net {

// DoDecode(STRUCTURE* out, QuicHttpDecodeBuffer* b) decodes the structure from
// start to end, advancing the cursor by STRUCTURE::EncodedSize(). The decode
// buffer must be large enough (i.e. b->Remaining() >=
// STRUCTURE::EncodedSize()).

QUIC_EXPORT_PRIVATE void DoDecode(QuicHttpFrameHeader* out,
                                  QuicHttpDecodeBuffer* b);
QUIC_EXPORT_PRIVATE void DoDecode(QuicHttpPriorityFields* out,
                                  QuicHttpDecodeBuffer* b);
QUIC_EXPORT_PRIVATE void DoDecode(QuicHttpRstStreamFields* out,
                                  QuicHttpDecodeBuffer* b);
QUIC_EXPORT_PRIVATE void DoDecode(QuicHttpSettingFields* out,
                                  QuicHttpDecodeBuffer* b);
QUIC_EXPORT_PRIVATE void DoDecode(QuicHttpPushPromiseFields* out,
                                  QuicHttpDecodeBuffer* b);
QUIC_EXPORT_PRIVATE void DoDecode(QuicHttpPingFields* out,
                                  QuicHttpDecodeBuffer* b);
QUIC_EXPORT_PRIVATE void DoDecode(QuicHttpGoAwayFields* out,
                                  QuicHttpDecodeBuffer* b);
QUIC_EXPORT_PRIVATE void DoDecode(QuicHttpWindowUpdateFields* out,
                                  QuicHttpDecodeBuffer* b);
QUIC_EXPORT_PRIVATE void DoDecode(QuicHttpAltSvcFields* out,
                                  QuicHttpDecodeBuffer* b);

}  // namespace net

#endif  // NET_QUIC_HTTP_DECODER_QUIC_HTTP_DECODE_STRUCTURES_H_
