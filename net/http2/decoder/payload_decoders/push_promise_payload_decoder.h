// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef NET_HTTP2_DECODER_PAYLOAD_DECODERS_PUSH_PROMISE_PAYLOAD_DECODER_H_
#define NET_HTTP2_DECODER_PAYLOAD_DECODERS_PUSH_PROMISE_PAYLOAD_DECODER_H_

// Decodes the payload of a PUSH_PROMISE frame.

#include "net/http2/decoder/decode_buffer.h"
#include "net/http2/decoder/decode_status.h"
#include "net/http2/decoder/frame_decoder_state.h"
#include "net/http2/http2_structures.h"
#include "net/http2/platform/api/http2_export.h"

namespace net {
namespace test {
class PushPromisePayloadDecoderPeer;
}  // namespace test

class HTTP2_EXPORT_PRIVATE PushPromisePayloadDecoder {
 public:
  // States during decoding of a PUSH_PROMISE frame.
  enum class PayloadState {
    // The frame is padded and we need to read the PAD_LENGTH field (1 byte).
    kReadPadLength,

    // Ready to start decoding the fixed size fields of the PUSH_PROMISE
    // frame into push_promise_fields_.
    kStartDecodingPushPromiseFields,

    // The decoder has already called OnPushPromiseStart, and is now reporting
    // the HPACK block fragment to the listener's OnHpackFragment method.
    kReadPayload,

    // The decoder has finished with the HPACK block fragment, and is now
    // ready to skip the trailing padding, if the frame has any.
    kSkipPadding,

    // The fixed size fields weren't all available when the decoder first tried
    // to decode them (state kStartDecodingPushPromiseFields); this state
    // resumes the decoding when ResumeDecodingPayload is called later.
    kResumeDecodingPushPromiseFields,
  };

  // Starts the decoding of a PUSH_PROMISE frame's payload, and completes it if
  // the entire payload is in the provided decode buffer.
  DecodeStatus StartDecodingPayload(FrameDecoderState* state, DecodeBuffer* db);

  // Resumes decoding a PUSH_PROMISE frame's payload that has been split across
  // decode buffers.
  DecodeStatus ResumeDecodingPayload(FrameDecoderState* state,
                                     DecodeBuffer* db);

 private:
  friend class test::PushPromisePayloadDecoderPeer;

  void ReportPushPromise(FrameDecoderState* state);

  PayloadState payload_state_;
  Http2PushPromiseFields push_promise_fields_;
};

}  // namespace net

#endif  // NET_HTTP2_DECODER_PAYLOAD_DECODERS_PUSH_PROMISE_PAYLOAD_DECODER_H_
