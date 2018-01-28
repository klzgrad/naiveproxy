// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef NET_QUIC_HTTP_DECODER_PAYLOAD_DECODERS_QUIC_HTTP_PUSH_PROMISE_PAYLOAD_DECODER_H_
#define NET_QUIC_HTTP_DECODER_PAYLOAD_DECODERS_QUIC_HTTP_PUSH_PROMISE_PAYLOAD_DECODER_H_

// Decodes the payload of a PUSH_PROMISE frame.
// See http://g3doc/gfe/quic/http/decoder/payload_decoders/README.md for info
// about payload decoders.

#include "net/quic/http/decoder/quic_http_decode_buffer.h"
#include "net/quic/http/decoder/quic_http_decode_status.h"
#include "net/quic/http/decoder/quic_http_frame_decoder_state.h"
#include "net/quic/http/quic_http_structures.h"
#include "net/quic/platform/api/quic_export.h"

namespace net {
namespace test {
class QuicHttpPushPromiseQuicHttpPayloadDecoderPeer;
}  // namespace test

class QUIC_EXPORT_PRIVATE QuicHttpPushPromiseQuicHttpPayloadDecoder {
 public:
  // States during decoding of a PUSH_PROMISE frame.
  enum class PayloadState {
    // The frame is padded and we need to read the PAD_LENGTH field (1 byte).
    kReadPadLength,

    // Ready to start decoding the fixed size fields of the PUSH_PROMISE
    // frame into push_promise_fields_.
    kStartDecodingPushPromiseFields,

    // The decoder has already called OnPushPromiseStart, and is now reporting
    // the HPQUIC_HTTP_ACK block fragment to the listener's OnHpackFragment
    // method.
    kReadPayload,

    // The decoder has finished with the HPQUIC_HTTP_ACK block fragment, and is
    // now
    // ready to skip the trailing padding, if the frame has any.
    kSkipPadding,

    // The fixed size fields weren't all available when the decoder first tried
    // to decode them (state kStartDecodingPushPromiseFields); this state
    // resumes the decoding when ResumeDecodingPayload is called later.
    kResumeDecodingPushPromiseFields,
  };

  // Starts the decoding of a PUSH_PROMISE frame's payload, and completes it if
  // the entire payload is in the provided decode buffer.
  QuicHttpDecodeStatus StartDecodingPayload(QuicHttpFrameDecoderState* state,
                                            QuicHttpDecodeBuffer* db);

  // Resumes decoding a PUSH_PROMISE frame's payload that has been split across
  // decode buffers.
  QuicHttpDecodeStatus ResumeDecodingPayload(QuicHttpFrameDecoderState* state,
                                             QuicHttpDecodeBuffer* db);

 private:
  friend class test::QuicHttpPushPromiseQuicHttpPayloadDecoderPeer;

  void ReportPushPromise(QuicHttpFrameDecoderState* state);

  PayloadState payload_state_;
  QuicHttpPushPromiseFields push_promise_fields_;
};

}  // namespace net

#endif  // NET_QUIC_HTTP_DECODER_PAYLOAD_DECODERS_QUIC_HTTP_PUSH_PROMISE_PAYLOAD_DECODER_H_
