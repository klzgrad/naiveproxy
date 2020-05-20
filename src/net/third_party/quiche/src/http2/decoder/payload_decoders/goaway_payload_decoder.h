// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef QUICHE_HTTP2_DECODER_PAYLOAD_DECODERS_GOAWAY_PAYLOAD_DECODER_H_
#define QUICHE_HTTP2_DECODER_PAYLOAD_DECODERS_GOAWAY_PAYLOAD_DECODER_H_

// Decodes the payload of a GOAWAY frame.

// TODO(jamessynge): Sweep through all payload decoders, changing the names of
// the PayloadState enums so that they are really states, and not actions.

#include "net/third_party/quiche/src/http2/decoder/decode_buffer.h"
#include "net/third_party/quiche/src/http2/decoder/decode_status.h"
#include "net/third_party/quiche/src/http2/decoder/frame_decoder_state.h"
#include "net/third_party/quiche/src/http2/http2_structures.h"
#include "net/third_party/quiche/src/common/platform/api/quiche_export.h"

namespace http2 {
namespace test {
class GoAwayPayloadDecoderPeer;
}  // namespace test

class QUICHE_EXPORT_PRIVATE GoAwayPayloadDecoder {
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

}  // namespace http2

#endif  // QUICHE_HTTP2_DECODER_PAYLOAD_DECODERS_GOAWAY_PAYLOAD_DECODER_H_
