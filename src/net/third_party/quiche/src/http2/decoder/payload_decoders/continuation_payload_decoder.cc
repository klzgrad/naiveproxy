// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/third_party/quiche/src/http2/decoder/payload_decoders/continuation_payload_decoder.h"

#include <stddef.h>

#include "net/third_party/quiche/src/http2/decoder/decode_buffer.h"
#include "net/third_party/quiche/src/http2/decoder/http2_frame_decoder_listener.h"
#include "net/third_party/quiche/src/http2/http2_constants.h"
#include "net/third_party/quiche/src/http2/http2_structures.h"
#include "net/third_party/quiche/src/http2/platform/api/http2_logging.h"

namespace http2 {

DecodeStatus ContinuationPayloadDecoder::StartDecodingPayload(
    FrameDecoderState* state,
    DecodeBuffer* db) {
  const Http2FrameHeader& frame_header = state->frame_header();
  const uint32_t total_length = frame_header.payload_length;

  HTTP2_DVLOG(2) << "ContinuationPayloadDecoder::StartDecodingPayload: "
                 << frame_header;
  DCHECK_EQ(Http2FrameType::CONTINUATION, frame_header.type);
  DCHECK_LE(db->Remaining(), total_length);
  DCHECK_EQ(0, frame_header.flags & ~(Http2FrameFlag::END_HEADERS));

  state->InitializeRemainders();
  state->listener()->OnContinuationStart(frame_header);
  return ResumeDecodingPayload(state, db);
}

DecodeStatus ContinuationPayloadDecoder::ResumeDecodingPayload(
    FrameDecoderState* state,
    DecodeBuffer* db) {
  HTTP2_DVLOG(2) << "ContinuationPayloadDecoder::ResumeDecodingPayload"
                 << "  remaining_payload=" << state->remaining_payload()
                 << "  db->Remaining=" << db->Remaining();
  DCHECK_EQ(Http2FrameType::CONTINUATION, state->frame_header().type);
  DCHECK_LE(state->remaining_payload(), state->frame_header().payload_length);
  DCHECK_LE(db->Remaining(), state->remaining_payload());

  size_t avail = db->Remaining();
  DCHECK_LE(avail, state->remaining_payload());
  if (avail > 0) {
    state->listener()->OnHpackFragment(db->cursor(), avail);
    db->AdvanceCursor(avail);
    state->ConsumePayload(avail);
  }
  if (state->remaining_payload() == 0) {
    state->listener()->OnContinuationEnd();
    return DecodeStatus::kDecodeDone;
  }
  return DecodeStatus::kDecodeInProgress;
}

}  // namespace http2
