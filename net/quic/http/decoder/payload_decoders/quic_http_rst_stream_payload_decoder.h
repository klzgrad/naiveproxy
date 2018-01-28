// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef NET_QUIC_HTTP_DECODER_PAYLOAD_DECODERS_QUIC_HTTP_RST_STREAM_PAYLOAD_DECODER_H_
#define NET_QUIC_HTTP_DECODER_PAYLOAD_DECODERS_QUIC_HTTP_RST_STREAM_PAYLOAD_DECODER_H_

// Decodes the payload of a RST_STREAM frame.
// See http://g3doc/gfe/quic/http/decoder/payload_decoders/README.md for info
// about payload decoders.

#include "net/quic/http/decoder/quic_http_decode_buffer.h"
#include "net/quic/http/decoder/quic_http_decode_status.h"
#include "net/quic/http/decoder/quic_http_frame_decoder_state.h"
#include "net/quic/http/quic_http_structures.h"
#include "net/quic/platform/api/quic_export.h"

namespace net {
namespace test {
class QuicHttpRstStreamQuicHttpPayloadDecoderPeer;
}  // namespace test

class QUIC_EXPORT_PRIVATE QuicHttpRstStreamQuicHttpPayloadDecoder {
 public:
  // Starts the decoding of a RST_STREAM frame's payload, and completes it if
  // the entire payload is in the provided decode buffer.
  QuicHttpDecodeStatus StartDecodingPayload(QuicHttpFrameDecoderState* state,
                                            QuicHttpDecodeBuffer* db);

  // Resumes decoding a RST_STREAM frame's payload that has been split across
  // decode buffers.
  QuicHttpDecodeStatus ResumeDecodingPayload(QuicHttpFrameDecoderState* state,
                                             QuicHttpDecodeBuffer* db);

 private:
  friend class test::QuicHttpRstStreamQuicHttpPayloadDecoderPeer;

  QuicHttpDecodeStatus HandleStatus(QuicHttpFrameDecoderState* state,
                                    QuicHttpDecodeStatus status);

  QuicHttpRstStreamFields rst_stream_fields_;
};

}  // namespace net

#endif  // NET_QUIC_HTTP_DECODER_PAYLOAD_DECODERS_QUIC_HTTP_RST_STREAM_PAYLOAD_DECODER_H_
