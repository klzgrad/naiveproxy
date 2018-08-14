// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef NET_THIRD_PARTY_HTTP2_DECODER_PAYLOAD_DECODERS_CONTINUATION_PAYLOAD_DECODER_H_
#define NET_THIRD_PARTY_HTTP2_DECODER_PAYLOAD_DECODERS_CONTINUATION_PAYLOAD_DECODER_H_

// Decodes the payload of a CONTINUATION frame.

#include "net/third_party/http2/decoder/decode_buffer.h"
#include "net/third_party/http2/decoder/decode_status.h"
#include "net/third_party/http2/decoder/frame_decoder_state.h"
#include "net/third_party/http2/platform/api/http2_export.h"

namespace http2 {

class HTTP2_EXPORT_PRIVATE ContinuationPayloadDecoder {
 public:
  // Starts the decoding of a CONTINUATION frame's payload, and completes
  // it if the entire payload is in the provided decode buffer.
  DecodeStatus StartDecodingPayload(FrameDecoderState* state, DecodeBuffer* db);

  // Resumes decoding a CONTINUATION frame's payload that has been split across
  // decode buffers.
  DecodeStatus ResumeDecodingPayload(FrameDecoderState* state,
                                     DecodeBuffer* db);
};

}  // namespace http2

#endif  // NET_THIRD_PARTY_HTTP2_DECODER_PAYLOAD_DECODERS_CONTINUATION_PAYLOAD_DECODER_H_
