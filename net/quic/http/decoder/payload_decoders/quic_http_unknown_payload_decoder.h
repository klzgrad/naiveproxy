// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef NET_QUIC_HTTP_DECODER_PAYLOAD_DECODERS_QUIC_HTTP_UNKNOWN_PAYLOAD_DECODER_H_
#define NET_QUIC_HTTP_DECODER_PAYLOAD_DECODERS_QUIC_HTTP_UNKNOWN_PAYLOAD_DECODER_H_

// Decodes the payload of a frame whose type unknown.  According to the HTTP/2
// specification (http://httpwg.org/specs/rfc7540.html#FrameHeader):
//     Implementations MUST ignore and discard any frame that has
//     a type that is unknown.
//
// See http://g3doc/gfe/quic/http/decoder/payload_decoders/README.md for info
// about payload decoders.

#include "net/quic/http/decoder/quic_http_decode_buffer.h"
#include "net/quic/http/decoder/quic_http_decode_status.h"
#include "net/quic/http/decoder/quic_http_frame_decoder_state.h"
#include "net/quic/platform/api/quic_export.h"

namespace net {

class QUIC_EXPORT_PRIVATE QuicHttpUnknownQuicHttpPayloadDecoder {
 public:
  // Starts decoding a payload of unknown type; just passes it to the listener.
  QuicHttpDecodeStatus StartDecodingPayload(QuicHttpFrameDecoderState* state,
                                            QuicHttpDecodeBuffer* db);

  // Resumes decoding a payload of unknown type that has been split across
  // decode buffers.
  QuicHttpDecodeStatus ResumeDecodingPayload(QuicHttpFrameDecoderState* state,
                                             QuicHttpDecodeBuffer* db);
};

}  // namespace net

#endif  // NET_QUIC_HTTP_DECODER_PAYLOAD_DECODERS_QUIC_HTTP_UNKNOWN_PAYLOAD_DECODER_H_
