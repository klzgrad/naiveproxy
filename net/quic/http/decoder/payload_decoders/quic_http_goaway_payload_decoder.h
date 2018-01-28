// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef NET_QUIC_HTTP_DECODER_PAYLOAD_DECODERS_QUIC_HTTP_GOAWAY_PAYLOAD_DECODER_H_
#define NET_QUIC_HTTP_DECODER_PAYLOAD_DECODERS_QUIC_HTTP_GOAWAY_PAYLOAD_DECODER_H_

// Decodes the payload of a GOAWAY frame.
// See http://g3doc/gfe/quic/http/decoder/payload_decoders/README.md for info
// about payload decoders.

// TODO(jamessynge): Sweep through all payload decoders, changing the names of
// the PayloadState enums so that they are really states, and not actions.

#include "net/quic/http/decoder/quic_http_decode_buffer.h"
#include "net/quic/http/decoder/quic_http_decode_status.h"
#include "net/quic/http/decoder/quic_http_frame_decoder_state.h"
#include "net/quic/http/quic_http_structures.h"
#include "net/quic/platform/api/quic_export.h"

namespace net {
namespace test {
class QuicHttpGoAwayQuicHttpPayloadDecoderPeer;
}  // namespace test

class QUIC_EXPORT_PRIVATE QuicHttpGoAwayQuicHttpPayloadDecoder {
 public:
  // States during decoding of a GOAWAY frame.
  enum class PayloadState {
    // At the start of the GOAWAY frame payload, ready to start decoding the
    // fixed size fields into goaway_fields_.
    kStartDecodingFixedFields,

    // Handle the QuicHttpDecodeStatus returned from starting or resuming the
    // decoding of QuicHttpGoAwayFields into goaway_fields_. If complete,
    // calls OnGoAwayStart.
    kHandleFixedFieldsStatus,

    // Report the Opaque Data portion of the payload to the listener's
    // OnGoAwayOpaqueData method, and call OnGoAwayEnd when the end of the
    // payload is reached.
    kReadOpaqueData,

    // The fixed size fields weren't all available when the decoder first
    // tried to decode them (state kStartDecodingFixedFields); this state
    // resumes the decoding when ResumeDecodingPayload is called later.
    kResumeDecodingFixedFields,
  };

  // Starts the decoding of a GOAWAY frame's payload, and completes it if
  // the entire payload is in the provided decode buffer.
  QuicHttpDecodeStatus StartDecodingPayload(QuicHttpFrameDecoderState* state,
                                            QuicHttpDecodeBuffer* db);

  // Resumes decoding a GOAWAY frame's payload that has been split across
  // decode buffers.
  QuicHttpDecodeStatus ResumeDecodingPayload(QuicHttpFrameDecoderState* state,
                                             QuicHttpDecodeBuffer* db);

 private:
  friend class test::QuicHttpGoAwayQuicHttpPayloadDecoderPeer;

  QuicHttpGoAwayFields goaway_fields_;
  PayloadState payload_state_;
};

}  // namespace net

#endif  // NET_QUIC_HTTP_DECODER_PAYLOAD_DECODERS_QUIC_HTTP_GOAWAY_PAYLOAD_DECODER_H_
