// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef QUICHE_HTTP2_DECODER_PAYLOAD_DECODERS_RST_STREAM_PAYLOAD_DECODER_H_
#define QUICHE_HTTP2_DECODER_PAYLOAD_DECODERS_RST_STREAM_PAYLOAD_DECODER_H_

// Decodes the payload of a RST_STREAM frame.

#include "quiche/http2/decoder/decode_buffer.h"
#include "quiche/http2/decoder/decode_status.h"
#include "quiche/http2/decoder/frame_decoder_state.h"
#include "quiche/http2/http2_structures.h"
#include "quiche/common/platform/api/quiche_export.h"

namespace http2 {
namespace test {
class RstStreamPayloadDecoderPeer;
}  // namespace test

class QUICHE_EXPORT RstStreamPayloadDecoder {
 public:
  // Starts the decoding of a RST_STREAM frame's payload, and completes it if
  // the entire payload is in the provided decode buffer.
  DecodeStatus StartDecodingPayload(FrameDecoderState* state, DecodeBuffer* db);

  // Resumes decoding a RST_STREAM frame's payload that has been split across
  // decode buffers.
  DecodeStatus ResumeDecodingPayload(FrameDecoderState* state,
                                     DecodeBuffer* db);

 private:
  friend class test::RstStreamPayloadDecoderPeer;

  DecodeStatus HandleStatus(FrameDecoderState* state, DecodeStatus status);

  Http2RstStreamFields rst_stream_fields_;
};

}  // namespace http2

#endif  // QUICHE_HTTP2_DECODER_PAYLOAD_DECODERS_RST_STREAM_PAYLOAD_DECODER_H_
