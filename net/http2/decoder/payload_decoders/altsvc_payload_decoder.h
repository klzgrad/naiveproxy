// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef NET_HTTP2_DECODER_PAYLOAD_DECODERS_ALTSVC_PAYLOAD_DECODER_H_
#define NET_HTTP2_DECODER_PAYLOAD_DECODERS_ALTSVC_PAYLOAD_DECODER_H_

// Decodes the payload of a ALTSVC frame.

#include "net/http2/decoder/decode_buffer.h"
#include "net/http2/decoder/decode_status.h"
#include "net/http2/decoder/frame_decoder_state.h"
#include "net/http2/http2_structures.h"
#include "net/http2/platform/api/http2_export.h"

namespace net {
namespace test {
class AltSvcPayloadDecoderPeer;
}  // namespace test

class HTTP2_EXPORT_PRIVATE AltSvcPayloadDecoder {
 public:
  // States during decoding of a ALTSVC frame.
  enum class PayloadState {
    // Start decoding the fixed size structure at the start of an ALTSVC
    // frame (Http2AltSvcFields).
    kStartDecodingStruct,

    // Handle the DecodeStatus returned from starting or resuming the
    // decoding of Http2AltSvcFields. If complete, calls OnAltSvcStart.
    kMaybeDecodedStruct,

    // Reports the value of the strings (origin and value) of an ALTSVC frame
    // to the listener.
    kDecodingStrings,

    // The initial decode buffer wasn't large enough for the Http2AltSvcFields,
    // so this state resumes the decoding when ResumeDecodingPayload is called
    // later with a new DecodeBuffer.
    kResumeDecodingStruct,
  };

  // Starts the decoding of a ALTSVC frame's payload, and completes it if the
  // entire payload is in the provided decode buffer.
  DecodeStatus StartDecodingPayload(FrameDecoderState* state, DecodeBuffer* db);

  // Resumes decoding a ALTSVC frame's payload that has been split across
  // decode buffers.
  DecodeStatus ResumeDecodingPayload(FrameDecoderState* state,
                                     DecodeBuffer* db);

 private:
  friend class test::AltSvcPayloadDecoderPeer;

  // Implements state kDecodingStrings.
  DecodeStatus DecodeStrings(FrameDecoderState* state, DecodeBuffer* db);

  Http2AltSvcFields altsvc_fields_;
  PayloadState payload_state_;
};

}  // namespace net

#endif  // NET_HTTP2_DECODER_PAYLOAD_DECODERS_ALTSVC_PAYLOAD_DECODER_H_
