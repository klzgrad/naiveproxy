// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef QUICHE_HTTP2_DECODER_PAYLOAD_DECODERS_WINDOW_UPDATE_PAYLOAD_DECODER_H_
#define QUICHE_HTTP2_DECODER_PAYLOAD_DECODERS_WINDOW_UPDATE_PAYLOAD_DECODER_H_

// Decodes the payload of a WINDOW_UPDATE frame.

#include "net/third_party/quiche/src/http2/decoder/decode_buffer.h"
#include "net/third_party/quiche/src/http2/decoder/decode_status.h"
#include "net/third_party/quiche/src/http2/decoder/frame_decoder_state.h"
#include "net/third_party/quiche/src/http2/http2_structures.h"
#include "net/third_party/quiche/src/common/platform/api/quiche_export.h"

namespace http2 {
namespace test {
class WindowUpdatePayloadDecoderPeer;
}  // namespace test

class QUICHE_EXPORT_PRIVATE WindowUpdatePayloadDecoder {
 public:
  // Starts decoding a WINDOW_UPDATE frame's payload, and completes it if
  // the entire payload is in the provided decode buffer.
  DecodeStatus StartDecodingPayload(FrameDecoderState* state, DecodeBuffer* db);

  // Resumes decoding a WINDOW_UPDATE frame's payload that has been split across
  // decode buffers.
  DecodeStatus ResumeDecodingPayload(FrameDecoderState* state,
                                     DecodeBuffer* db);

 private:
  friend class test::WindowUpdatePayloadDecoderPeer;

  DecodeStatus HandleStatus(FrameDecoderState* state, DecodeStatus status);

  Http2WindowUpdateFields window_update_fields_;
};

}  // namespace http2

#endif  // QUICHE_HTTP2_DECODER_PAYLOAD_DECODERS_WINDOW_UPDATE_PAYLOAD_DECODER_H_
