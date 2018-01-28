// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef NET_QUIC_HTTP_DECODER_PAYLOAD_DECODERS_QUIC_HTTP_ALTSVC_PAYLOAD_DECODER_H_
#define NET_QUIC_HTTP_DECODER_PAYLOAD_DECODERS_QUIC_HTTP_ALTSVC_PAYLOAD_DECODER_H_

// Decodes the payload of a ALTSVC frame.
// See http://g3doc/gfe/quic/http/decoder/payload_decoders/README.md for info
// about payload decoders.

#include "net/quic/http/decoder/quic_http_decode_buffer.h"
#include "net/quic/http/decoder/quic_http_decode_status.h"
#include "net/quic/http/decoder/quic_http_frame_decoder_state.h"
#include "net/quic/http/quic_http_structures.h"
#include "net/quic/platform/api/quic_export.h"

namespace net {
namespace test {
class QuicHttpAltSvcQuicHttpPayloadDecoderPeer;
}  // namespace test

class QUIC_EXPORT_PRIVATE QuicHttpAltSvcQuicHttpPayloadDecoder {
 public:
  // States during decoding of a ALTSVC frame.
  enum class PayloadState {
    // Start decoding the fixed size structure at the start of an ALTSVC
    // frame (QuicHttpAltSvcFields).
    kStartDecodingStruct,

    // Handle the QuicHttpDecodeStatus returned from starting or resuming the
    // decoding of QuicHttpAltSvcFields. If complete, calls OnAltSvcStart.
    kMaybeDecodedStruct,

    // Reports the value of the std::strings (origin and value) of an ALTSVC
    // frame
    // to the listener.
    kDecodingStrings,

    // The initial decode buffer wasn't large enough for the
    // QuicHttpAltSvcFields,
    // so this state resumes the decoding when ResumeDecodingPayload is called
    // later with a new QuicHttpDecodeBuffer.
    kResumeDecodingStruct,
  };

  // Starts the decoding of a ALTSVC frame's payload, and completes it if the
  // entire payload is in the provided decode buffer.
  QuicHttpDecodeStatus StartDecodingPayload(QuicHttpFrameDecoderState* state,
                                            QuicHttpDecodeBuffer* db);

  // Resumes decoding a ALTSVC frame's payload that has been split across
  // decode buffers.
  QuicHttpDecodeStatus ResumeDecodingPayload(QuicHttpFrameDecoderState* state,
                                             QuicHttpDecodeBuffer* db);

 private:
  friend class test::QuicHttpAltSvcQuicHttpPayloadDecoderPeer;

  // Implements state kDecodingStrings.
  QuicHttpDecodeStatus DecodeStrings(QuicHttpFrameDecoderState* state,
                                     QuicHttpDecodeBuffer* db);

  QuicHttpAltSvcFields altsvc_fields_;
  PayloadState payload_state_;
};

}  // namespace net

#endif  // NET_QUIC_HTTP_DECODER_PAYLOAD_DECODERS_QUIC_HTTP_ALTSVC_PAYLOAD_DECODER_H_
