// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/quic/http/decoder/payload_decoders/quic_http_continuation_payload_decoder.h"

#include <stddef.h>

#include <cstdint>

#include "base/logging.h"
#include "net/quic/http/decoder/quic_http_decode_buffer.h"
#include "net/quic/http/decoder/quic_http_frame_decoder_listener.h"
#include "net/quic/http/quic_http_constants.h"
#include "net/quic/http/quic_http_structures.h"

namespace net {

QuicHttpDecodeStatus
QuicHttpContinuationQuicHttpPayloadDecoder::StartDecodingPayload(
    QuicHttpFrameDecoderState* state,
    QuicHttpDecodeBuffer* db) {
  const QuicHttpFrameHeader& frame_header = state->frame_header();
  const uint32_t total_length = frame_header.payload_length;

  DVLOG(2)
      << "QuicHttpContinuationQuicHttpPayloadDecoder::StartDecodingPayload: "
      << frame_header;
  DCHECK_EQ(QuicHttpFrameType::CONTINUATION, frame_header.type);
  DCHECK_LE(db->Remaining(), total_length);
  DCHECK_EQ(0,
            frame_header.flags & ~(QuicHttpFrameFlag::QUIC_HTTP_END_HEADERS));

  state->InitializeRemainders();
  state->listener()->OnContinuationStart(frame_header);
  return ResumeDecodingPayload(state, db);
}

QuicHttpDecodeStatus
QuicHttpContinuationQuicHttpPayloadDecoder::ResumeDecodingPayload(
    QuicHttpFrameDecoderState* state,
    QuicHttpDecodeBuffer* db) {
  DVLOG(2)
      << "QuicHttpContinuationQuicHttpPayloadDecoder::ResumeDecodingPayload"
      << "  remaining_payload=" << state->remaining_payload()
      << "  db->Remaining=" << db->Remaining();
  DCHECK_EQ(QuicHttpFrameType::CONTINUATION, state->frame_header().type);
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
    return QuicHttpDecodeStatus::kDecodeDone;
  }
  return QuicHttpDecodeStatus::kDecodeInProgress;
}

}  // namespace net
