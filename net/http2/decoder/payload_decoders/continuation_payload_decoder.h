// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef NET_HTTP2_DECODER_PAYLOAD_DECODERS_CONTINUATION_PAYLOAD_DECODER_H_
#define NET_HTTP2_DECODER_PAYLOAD_DECODERS_CONTINUATION_PAYLOAD_DECODER_H_

// Decodes the payload of a CONTINUATION frame.

#include "net/http2/decoder/decode_buffer.h"
#include "net/http2/decoder/decode_status.h"
#include "net/http2/decoder/frame_decoder_state.h"
#include "net/http2/platform/api/http2_export.h"

namespace net {

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

}  // namespace net

#endif  // NET_HTTP2_DECODER_PAYLOAD_DECODERS_CONTINUATION_PAYLOAD_DECODER_H_
