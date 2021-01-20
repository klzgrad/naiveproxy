// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef QUICHE_HTTP2_DECODER_PAYLOAD_DECODERS_UNKNOWN_PAYLOAD_DECODER_H_
#define QUICHE_HTTP2_DECODER_PAYLOAD_DECODERS_UNKNOWN_PAYLOAD_DECODER_H_

// Decodes the payload of a frame whose type unknown.  According to the HTTP/2
// specification (http://httpwg.org/specs/rfc7540.html#FrameHeader):
//     Implementations MUST ignore and discard any frame that has
//     a type that is unknown.

#include "net/third_party/quiche/src/http2/decoder/decode_buffer.h"
#include "net/third_party/quiche/src/http2/decoder/decode_status.h"
#include "net/third_party/quiche/src/http2/decoder/frame_decoder_state.h"
#include "net/third_party/quiche/src/common/platform/api/quiche_export.h"

namespace http2 {

class QUICHE_EXPORT_PRIVATE UnknownPayloadDecoder {
 public:
  // Starts decoding a payload of unknown type; just passes it to the listener.
  DecodeStatus StartDecodingPayload(FrameDecoderState* state, DecodeBuffer* db);

  // Resumes decoding a payload of unknown type that has been split across
  // decode buffers.
  DecodeStatus ResumeDecodingPayload(FrameDecoderState* state,
                                     DecodeBuffer* db);
};

}  // namespace http2

#endif  // QUICHE_HTTP2_DECODER_PAYLOAD_DECODERS_UNKNOWN_PAYLOAD_DECODER_H_
