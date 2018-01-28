// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef NET_HTTP2_DECODER_PAYLOAD_DECODERS_GOAWAY_PAYLOAD_DECODER_H_
#define NET_HTTP2_DECODER_PAYLOAD_DECODERS_GOAWAY_PAYLOAD_DECODER_H_

// Decodes the payload of a GOAWAY frame.

// TODO(jamessynge): Sweep through all payload decoders, changing the names of
// the PayloadState enums so that they are really states, and not actions.

#include "net/http2/decoder/decode_buffer.h"
#include "net/http2/decoder/decode_status.h"
#include "net/http2/decoder/frame_decoder_state.h"
#include "net/http2/http2_structures.h"
#include "net/http2/platform/api/http2_export.h"

namespace net {
namespace test {
class GoAwayPayloadDecoderPeer;
}  // namespace test

class HTTP2_EXPORT_PRIVATE GoAwayPayloadDecoder {
 public:
  // States during decoding of a GOAWAY frame.
  enum class PayloadState {
    // At the start of the GOAWAY frame payload, ready to start decoding the
    // fixed size fields into goaway_fields_.
    kStartDecodingFixedFields,

    // Handle the DecodeStatus returned from starting or resuming the
    // decoding of Http2GoAwayFields into goaway_fields_. If complete,
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
  DecodeStatus StartDecodingPayload(FrameDecoderState* state, DecodeBuffer* db);

  // Resumes decoding a GOAWAY frame's payload that has been split across
  // decode buffers.
  DecodeStatus ResumeDecodingPayload(FrameDecoderState* state,
                                     DecodeBuffer* db);

 private:
  friend class test::GoAwayPayloadDecoderPeer;

  Http2GoAwayFields goaway_fields_;
  PayloadState payload_state_;
};

}  // namespace net

#endif  // NET_HTTP2_DECODER_PAYLOAD_DECODERS_GOAWAY_PAYLOAD_DECODER_H_
