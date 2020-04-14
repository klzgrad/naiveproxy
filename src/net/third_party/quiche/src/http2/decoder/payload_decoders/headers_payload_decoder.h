// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef QUICHE_HTTP2_DECODER_PAYLOAD_DECODERS_HEADERS_PAYLOAD_DECODER_H_
#define QUICHE_HTTP2_DECODER_PAYLOAD_DECODERS_HEADERS_PAYLOAD_DECODER_H_

// Decodes the payload of a HEADERS frame.

#include "net/third_party/quiche/src/http2/decoder/decode_buffer.h"
#include "net/third_party/quiche/src/http2/decoder/decode_status.h"
#include "net/third_party/quiche/src/http2/decoder/frame_decoder_state.h"
#include "net/third_party/quiche/src/http2/http2_structures.h"
#include "net/third_party/quiche/src/common/platform/api/quiche_export.h"

namespace http2 {
namespace test {
class HeadersPayloadDecoderPeer;
}  // namespace test

class QUICHE_EXPORT_PRIVATE HeadersPayloadDecoder {
 public:
  // States during decoding of a HEADERS frame, unless the fast path kicks
  // in, in which case the state machine will be bypassed.
  enum class PayloadState {
    // The PADDED flag is set, and we now need to read the Pad Length field
    // (the first byte of the payload, after the common frame header).
    kReadPadLength,

    // The PRIORITY flag is set, and we now need to read the fixed size priority
    // fields (E, Stream Dependency, Weight) into priority_fields_.  Calls on
    // OnHeadersPriority if completely decodes those fields.
    kStartDecodingPriorityFields,

    // The decoder passes the non-padding portion of the remaining payload
    // (i.e. the HPACK block fragment) to the listener's OnHpackFragment method.
    kReadPayload,

    // The decoder has finished with the HPACK block fragment, and is now
    // ready to skip the trailing padding, if the frame has any.
    kSkipPadding,

    // The fixed size fields weren't all available when the decoder first tried
    // to decode them (state kStartDecodingPriorityFields); this state resumes
    // the decoding when ResumeDecodingPayload is called later.
    kResumeDecodingPriorityFields,
  };

  // Starts the decoding of a HEADERS frame's payload, and completes it if
  // the entire payload is in the provided decode buffer.
  DecodeStatus StartDecodingPayload(FrameDecoderState* state, DecodeBuffer* db);

  // Resumes decoding a HEADERS frame's payload that has been split across
  // decode buffers.
  DecodeStatus ResumeDecodingPayload(FrameDecoderState* state,
                                     DecodeBuffer* db);

 private:
  friend class test::HeadersPayloadDecoderPeer;

  PayloadState payload_state_;
  Http2PriorityFields priority_fields_;
};

}  // namespace http2

#endif  // QUICHE_HTTP2_DECODER_PAYLOAD_DECODERS_HEADERS_PAYLOAD_DECODER_H_
