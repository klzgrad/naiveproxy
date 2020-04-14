// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef QUICHE_HTTP2_DECODER_PAYLOAD_DECODERS_SETTINGS_PAYLOAD_DECODER_H_
#define QUICHE_HTTP2_DECODER_PAYLOAD_DECODERS_SETTINGS_PAYLOAD_DECODER_H_

// Decodes the payload of a SETTINGS frame; for the RFC, see:
//     http://httpwg.org/specs/rfc7540.html#SETTINGS

#include "net/third_party/quiche/src/http2/decoder/decode_buffer.h"
#include "net/third_party/quiche/src/http2/decoder/decode_status.h"
#include "net/third_party/quiche/src/http2/decoder/frame_decoder_state.h"
#include "net/third_party/quiche/src/http2/http2_structures.h"
#include "net/third_party/quiche/src/common/platform/api/quiche_export.h"

namespace http2 {
namespace test {
class SettingsPayloadDecoderPeer;
}  // namespace test

class QUICHE_EXPORT_PRIVATE SettingsPayloadDecoder {
 public:
  // Starts the decoding of a SETTINGS frame's payload, and completes it if
  // the entire payload is in the provided decode buffer.
  DecodeStatus StartDecodingPayload(FrameDecoderState* state, DecodeBuffer* db);

  // Resumes decoding a SETTINGS frame that has been split across decode
  // buffers.
  DecodeStatus ResumeDecodingPayload(FrameDecoderState* state,
                                     DecodeBuffer* db);

 private:
  friend class test::SettingsPayloadDecoderPeer;

  // Decodes as many settings as are available in the decode buffer, starting at
  // the first byte of one setting; if a single setting is split across buffers,
  // ResumeDecodingPayload will handle starting from where the previous call
  // left off, and then will call StartDecodingSettings.
  DecodeStatus StartDecodingSettings(FrameDecoderState* state,
                                     DecodeBuffer* db);

  // Decoding a single SETTING returned a status other than kDecodeDone; this
  // method just brings together the DCHECKs to reduce duplication.
  DecodeStatus HandleNotDone(FrameDecoderState* state,
                             DecodeBuffer* db,
                             DecodeStatus status);

  Http2SettingFields setting_fields_;
};

}  // namespace http2

#endif  // QUICHE_HTTP2_DECODER_PAYLOAD_DECODERS_SETTINGS_PAYLOAD_DECODER_H_
