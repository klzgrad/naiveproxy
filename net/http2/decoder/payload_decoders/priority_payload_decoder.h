// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef NET_HTTP2_DECODER_PAYLOAD_DECODERS_PRIORITY_PAYLOAD_DECODER_H_
#define NET_HTTP2_DECODER_PAYLOAD_DECODERS_PRIORITY_PAYLOAD_DECODER_H_

// Decodes the payload of a PRIORITY frame.

#include "net/http2/decoder/decode_buffer.h"
#include "net/http2/decoder/decode_status.h"
#include "net/http2/decoder/frame_decoder_state.h"
#include "net/http2/http2_structures.h"
#include "net/http2/platform/api/http2_export.h"

namespace net {
namespace test {
class PriorityPayloadDecoderPeer;
}  // namespace test

class HTTP2_EXPORT_PRIVATE PriorityPayloadDecoder {
 public:
  // Starts the decoding of a PRIORITY frame's payload, and completes it if
  // the entire payload is in the provided decode buffer.
  DecodeStatus StartDecodingPayload(FrameDecoderState* state, DecodeBuffer* db);

  // Resumes decoding a PRIORITY frame that has been split across decode
  // buffers.
  DecodeStatus ResumeDecodingPayload(FrameDecoderState* state,
                                     DecodeBuffer* db);

 private:
  friend class test::PriorityPayloadDecoderPeer;

  // Determines whether to report the PRIORITY to the listener, wait for more
  // input, or to report a Frame Size Error.
  DecodeStatus HandleStatus(FrameDecoderState* state, DecodeStatus status);

  Http2PriorityFields priority_fields_;
};

}  // namespace net

#endif  // NET_HTTP2_DECODER_PAYLOAD_DECODERS_PRIORITY_PAYLOAD_DECODER_H_
