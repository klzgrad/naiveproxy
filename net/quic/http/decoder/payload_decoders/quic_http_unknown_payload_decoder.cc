// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/quic/http/decoder/payload_decoders/quic_http_unknown_payload_decoder.h"

#include <stddef.h>

#include "base/logging.h"
#include "net/quic/http/decoder/quic_http_decode_buffer.h"
#include "net/quic/http/decoder/quic_http_frame_decoder_listener.h"
#include "net/quic/http/quic_http_constants.h"
#include "net/quic/http/quic_http_structures.h"

namespace net {

QuicHttpDecodeStatus
QuicHttpUnknownQuicHttpPayloadDecoder::StartDecodingPayload(
    QuicHttpFrameDecoderState* state,
    QuicHttpDecodeBuffer* db) {
  const QuicHttpFrameHeader& frame_header = state->frame_header();

  DVLOG(2) << "QuicHttpUnknownQuicHttpPayloadDecoder::StartDecodingPayload: "
           << frame_header;
  DCHECK(!IsSupportedQuicHttpFrameType(frame_header.type)) << frame_header;
  DCHECK_LE(db->Remaining(), frame_header.payload_length);

  state->InitializeRemainders();
  state->listener()->OnUnknownStart(frame_header);
  return ResumeDecodingPayload(state, db);
}

QuicHttpDecodeStatus
QuicHttpUnknownQuicHttpPayloadDecoder::ResumeDecodingPayload(
    QuicHttpFrameDecoderState* state,
    QuicHttpDecodeBuffer* db) {
  DVLOG(2) << "QuicHttpUnknownQuicHttpPayloadDecoder::ResumeDecodingPayload "
           << "remaining_payload=" << state->remaining_payload()
           << "; db->Remaining=" << db->Remaining();
  DCHECK(!IsSupportedQuicHttpFrameType(state->frame_header().type))
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
    return QuicHttpDecodeStatus::kDecodeDone;
  }
  return QuicHttpDecodeStatus::kDecodeInProgress;
}

}  // namespace net
