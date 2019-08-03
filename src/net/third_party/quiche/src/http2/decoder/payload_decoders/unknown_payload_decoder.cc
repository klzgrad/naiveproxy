// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/third_party/quiche/src/http2/decoder/payload_decoders/unknown_payload_decoder.h"

#include <stddef.h>

#include "net/third_party/quiche/src/http2/decoder/decode_buffer.h"
#include "net/third_party/quiche/src/http2/decoder/http2_frame_decoder_listener.h"
#include "net/third_party/quiche/src/http2/http2_constants.h"
#include "net/third_party/quiche/src/http2/http2_structures.h"
#include "net/third_party/quiche/src/http2/platform/api/http2_logging.h"

namespace http2 {

DecodeStatus UnknownPayloadDecoder::StartDecodingPayload(
    FrameDecoderState* state,
    DecodeBuffer* db) {
  const Http2FrameHeader& frame_header = state->frame_header();

  HTTP2_DVLOG(2) << "UnknownPayloadDecoder::StartDecodingPayload: "
                 << frame_header;
  DCHECK(!IsSupportedHttp2FrameType(frame_header.type)) << frame_header;
  DCHECK_LE(db->Remaining(), frame_header.payload_length);

  state->InitializeRemainders();
  state->listener()->OnUnknownStart(frame_header);
  return ResumeDecodingPayload(state, db);
}

DecodeStatus UnknownPayloadDecoder::ResumeDecodingPayload(
    FrameDecoderState* state,
    DecodeBuffer* db) {
  HTTP2_DVLOG(2) << "UnknownPayloadDecoder::ResumeDecodingPayload "
                 << "remaining_payload=" << state->remaining_payload()
                 << "; db->Remaining=" << db->Remaining();
  DCHECK(!IsSupportedHttp2FrameType(state->frame_header().type))
      << state->frame_header();
  DCHECK_LE(state->remaining_payload(), state->frame_header().payload_length);
  DCHECK_LE(db->Remaining(), state->remaining_payload());

  size_t avail = db->Remaining();
  if (avail > 0) {
    state->listener()->OnUnknownPayload(db->cursor(), avail);
    db->AdvanceCursor(avail);
    state->ConsumePayload(avail);
  }
  if (state->remaining_payload() == 0) {
    state->listener()->OnUnknownEnd();
    return DecodeStatus::kDecodeDone;
  }
  return DecodeStatus::kDecodeInProgress;
}

}  // namespace http2
