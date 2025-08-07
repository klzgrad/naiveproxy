// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef QUICHE_HTTP2_DECODER_PAYLOAD_DECODERS_PING_PAYLOAD_DECODER_H_
#define QUICHE_HTTP2_DECODER_PAYLOAD_DECODERS_PING_PAYLOAD_DECODER_H_

// Decodes the payload of a PING frame; for the RFC, see:
//     http://httpwg.org/specs/rfc7540.html#PING

#include "quiche/http2/core/http2_structures.h"
#include "quiche/http2/decoder/decode_buffer.h"
#include "quiche/http2/decoder/decode_status.h"
#include "quiche/http2/decoder/frame_decoder_state.h"
#include "quiche/common/platform/api/quiche_export.h"

namespace http2 {
namespace test {
class PingPayloadDecoderPeer;
}  // namespace test

class QUICHE_EXPORT PingPayloadDecoder {
 public:
  // Starts the decoding of a PING frame's payload, and completes it if the
  // entire payload is in the provided decode buffer.
  DecodeStatus StartDecodingPayload(FrameDecoderState* state, DecodeBuffer* db);

  // Resumes decoding a PING frame's payload that has been split across
  // decode buffers.
  DecodeStatus ResumeDecodingPayload(FrameDecoderState* state,
                                     DecodeBuffer* db);

 private:
  friend class test::PingPayloadDecoderPeer;

  DecodeStatus HandleStatus(FrameDecoderState* state, DecodeStatus status);

  Http2PingFields ping_fields_;
};

}  // namespace http2

#endif  // QUICHE_HTTP2_DECODER_PAYLOAD_DECODERS_PING_PAYLOAD_DECODER_H_
