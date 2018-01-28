// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef NET_QUIC_HTTP_DECODER_PAYLOAD_DECODERS_QUIC_HTTP_DATA_PAYLOAD_DECODER_H_
#define NET_QUIC_HTTP_DECODER_PAYLOAD_DECODERS_QUIC_HTTP_DATA_PAYLOAD_DECODER_H_

// Decodes the payload of a DATA frame.
// See http://g3doc/gfe/quic/http/decoder/payload_decoders/README.md for info
// about payload decoders.

#include "net/quic/http/decoder/quic_http_decode_buffer.h"
#include "net/quic/http/decoder/quic_http_decode_status.h"
#include "net/quic/http/decoder/quic_http_frame_decoder_state.h"
#include "net/quic/platform/api/quic_export.h"

namespace net {
namespace test {
class QuicHttpDataQuicHttpPayloadDecoderPeer;
}  // namespace test

class QUIC_EXPORT_PRIVATE QuicHttpDataQuicHttpPayloadDecoder {
 public:
  // States during decoding of a DATA frame.
  enum class PayloadState {
    // The frame is padded and we need to read the PAD_LENGTH field (1 byte),
    // and then call OnPadLength
    kReadPadLength,

    // Report the non-padding portion of the payload to the listener's
    // OnDataPayload method.
    kReadPayload,

    // The decoder has finished with the non-padding portion of the payload,
    // and is now ready to skip the trailing padding, if the frame has any.
    kSkipPadding,
  };

  // Starts decoding a DATA frame's payload, and completes it if
  // the entire payload is in the provided decode buffer.
  QuicHttpDecodeStatus StartDecodingPayload(QuicHttpFrameDecoderState* state,
                                            QuicHttpDecodeBuffer* db);

  // Resumes decoding a DATA frame's payload that has been split across
  // decode buffers.
  QuicHttpDecodeStatus ResumeDecodingPayload(QuicHttpFrameDecoderState* state,
                                             QuicHttpDecodeBuffer* db);

 private:
  friend class test::QuicHttpDataQuicHttpPayloadDecoderPeer;

  PayloadState payload_state_;
};

}  // namespace net

#endif  // NET_QUIC_HTTP_DECODER_PAYLOAD_DECODERS_QUIC_HTTP_DATA_PAYLOAD_DECODER_H_
