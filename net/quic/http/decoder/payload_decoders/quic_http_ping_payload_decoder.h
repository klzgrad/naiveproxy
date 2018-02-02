// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef NET_QUIC_HTTP_DECODER_PAYLOAD_DECODERS_QUIC_HTTP_PING_PAYLOAD_DECODER_H_
#define NET_QUIC_HTTP_DECODER_PAYLOAD_DECODERS_QUIC_HTTP_PING_PAYLOAD_DECODER_H_

// Decodes the payload of a PING frame; for the RFC, see:
//     http://httpwg.org/specs/rfc7540.html#PING
//
// For info about payload decoders, see:
//     http://g3doc/gfe/quic/http/decoder/payload_decoders/README.md

#include "net/quic/http/decoder/quic_http_decode_buffer.h"
#include "net/quic/http/decoder/quic_http_decode_status.h"
#include "net/quic/http/decoder/quic_http_frame_decoder_state.h"
#include "net/quic/http/quic_http_structures.h"
#include "net/quic/platform/api/quic_export.h"

namespace net {
namespace test {
class QuicHttpPingQuicHttpPayloadDecoderPeer;
}  // namespace test

class QUIC_EXPORT_PRIVATE QuicHttpPingQuicHttpPayloadDecoder {
 public:
  // Starts the decoding of a PING frame's payload, and completes it if the
  // entire payload is in the provided decode buffer.
  QuicHttpDecodeStatus StartDecodingPayload(QuicHttpFrameDecoderState* state,
                                            QuicHttpDecodeBuffer* db);

  // Resumes decoding a PING frame's payload that has been split across
  // decode buffers.
  QuicHttpDecodeStatus ResumeDecodingPayload(QuicHttpFrameDecoderState* state,
                                             QuicHttpDecodeBuffer* db);

 private:
  friend class test::QuicHttpPingQuicHttpPayloadDecoderPeer;

  QuicHttpDecodeStatus HandleStatus(QuicHttpFrameDecoderState* state,
                                    QuicHttpDecodeStatus status);

  QuicHttpPingFields ping_fields_;
};

}  // namespace net

#endif  // NET_QUIC_HTTP_DECODER_PAYLOAD_DECODERS_QUIC_HTTP_PING_PAYLOAD_DECODER_H_
