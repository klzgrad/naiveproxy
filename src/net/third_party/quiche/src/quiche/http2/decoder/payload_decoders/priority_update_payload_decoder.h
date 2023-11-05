// Copyright 2020 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef QUICHE_HTTP2_DECODER_PAYLOAD_DECODERS_PRIORITY_UPDATE_PAYLOAD_DECODER_H_
#define QUICHE_HTTP2_DECODER_PAYLOAD_DECODERS_PRIORITY_UPDATE_PAYLOAD_DECODER_H_

// Decodes the payload of a PRIORITY_UPDATE frame.

#include "quiche/http2/decoder/decode_buffer.h"
#include "quiche/http2/decoder/decode_status.h"
#include "quiche/http2/decoder/frame_decoder_state.h"
#include "quiche/http2/http2_structures.h"
#include "quiche/common/platform/api/quiche_export.h"

namespace http2 {
namespace test {
class PriorityUpdatePayloadDecoderPeer;
}  // namespace test

class QUICHE_EXPORT PriorityUpdatePayloadDecoder {
 public:
  // States during decoding of a PRIORITY_UPDATE frame.
  enum class PayloadState {
    // At the start of the PRIORITY_UPDATE frame payload, ready to start
    // decoding the fixed size fields into priority_update_fields_.
    kStartDecodingFixedFields,

    // The fixed size fields weren't all available when the decoder first
    // tried to decode them; this state resumes the decoding when
    // ResumeDecodingPayload is called later.
    kResumeDecodingFixedFields,

    // Handle the DecodeStatus returned from starting or resuming the decoding
    // of Http2PriorityUpdateFields into priority_update_fields_. If complete,
    // calls OnPriorityUpdateStart.
    kHandleFixedFieldsStatus,

    // Report the Priority Field Value portion of the payload to the listener's
    // OnPriorityUpdatePayload method, and call OnPriorityUpdateEnd when the end
    // of the payload is reached.
    kReadPriorityFieldValue,
  };

  // Starts the decoding of a PRIORITY_UPDATE frame's payload, and completes it
  // if the entire payload is in the provided decode buffer.
  DecodeStatus StartDecodingPayload(FrameDecoderState* state, DecodeBuffer* db);

  // Resumes decoding a PRIORITY_UPDATE frame that has been split across decode
  // buffers.
  DecodeStatus ResumeDecodingPayload(FrameDecoderState* state,
                                     DecodeBuffer* db);

 private:
  friend class test::PriorityUpdatePayloadDecoderPeer;

  Http2PriorityUpdateFields priority_update_fields_;
  PayloadState payload_state_;
};

}  // namespace http2

#endif  // QUICHE_HTTP2_DECODER_PAYLOAD_DECODERS_PRIORITY_UPDATE_PAYLOAD_DECODER_H_
