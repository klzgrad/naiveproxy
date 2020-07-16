// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef QUICHE_HTTP2_DECODER_PAYLOAD_DECODERS_DATA_PAYLOAD_DECODER_H_
#define QUICHE_HTTP2_DECODER_PAYLOAD_DECODERS_DATA_PAYLOAD_DECODER_H_

// Decodes the payload of a DATA frame.

#include "net/third_party/quiche/src/http2/decoder/decode_buffer.h"
#include "net/third_party/quiche/src/http2/decoder/decode_status.h"
#include "net/third_party/quiche/src/http2/decoder/frame_decoder_state.h"
#include "net/third_party/quiche/src/common/platform/api/quiche_export.h"

namespace http2 {
namespace test {
class DataPayloadDecoderPeer;
}  // namespace test

class QUICHE_EXPORT_PRIVATE DataPayloadDecoder {
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
  DecodeStatus StartDecodingPayload(FrameDecoderState* state, DecodeBuffer* db);

  // Resumes decoding a DATA frame's payload that has been split across
  // decode buffers.
  DecodeStatus ResumeDecodingPayload(FrameDecoderState* state,
                                     DecodeBuffer* db);

 private:
  friend class test::DataPayloadDecoderPeer;

  PayloadState payload_state_;
};

}  // namespace http2

#endif  // QUICHE_HTTP2_DECODER_PAYLOAD_DECODERS_DATA_PAYLOAD_DECODER_H_
