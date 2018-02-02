// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/quic/http/decoder/payload_decoders/quic_http_rst_stream_payload_decoder.h"

#include "base/logging.h"
#include "net/quic/http/decoder/quic_http_decode_buffer.h"
#include "net/quic/http/decoder/quic_http_frame_decoder_listener.h"
#include "net/quic/http/quic_http_constants.h"
#include "net/quic/http/quic_http_structures.h"

namespace net {

QuicHttpDecodeStatus
QuicHttpRstStreamQuicHttpPayloadDecoder::StartDecodingPayload(
    QuicHttpFrameDecoderState* state,
    QuicHttpDecodeBuffer* db) {
  DVLOG(2) << "QuicHttpRstStreamQuicHttpPayloadDecoder::StartDecodingPayload: "
           << state->frame_header();
  DCHECK_EQ(QuicHttpFrameType::RST_STREAM, state->frame_header().type);
  DCHECK_LE(db->Remaining(), state->frame_header().payload_length);
  // RST_STREAM has no flags.
  DCHECK_EQ(0, state->frame_header().flags);
  state->InitializeRemainders();
  return HandleStatus(
      state, state->StartDecodingStructureInPayload(&rst_stream_fields_, db));
}

QuicHttpDecodeStatus
QuicHttpRstStreamQuicHttpPayloadDecoder::ResumeDecodingPayload(
    QuicHttpFrameDecoderState* state,
    QuicHttpDecodeBuffer* db) {
  DVLOG(2) << "QuicHttpRstStreamQuicHttpPayloadDecoder::ResumeDecodingPayload"
           << "  remaining_payload=" << state->remaining_payload()
           << "  db->Remaining=" << db->Remaining();
  DCHECK_EQ(QuicHttpFrameType::RST_STREAM, state->frame_header().type);
  DCHECK_LE(db->Remaining(), state->frame_header().payload_length);
  return HandleStatus(
      state, state->ResumeDecodingStructureInPayload(&rst_stream_fields_, db));
}

QuicHttpDecodeStatus QuicHttpRstStreamQuicHttpPayloadDecoder::HandleStatus(
    QuicHttpFrameDecoderState* state,
    QuicHttpDecodeStatus status) {
  DVLOG(2) << "HandleStatus: status=" << status
           << "; remaining_payload=" << state->remaining_payload();
  if (status == QuicHttpDecodeStatus::kDecodeDone) {
    if (state->remaining_payload() == 0) {
      state->listener()->OnRstStream(state->frame_header(),
                                     rst_stream_fields_.error_code);
      return QuicHttpDecodeStatus::kDecodeDone;
    }
    // Payload is too long.
    return state->ReportFrameSizeError();
  }
  // Not done decoding the structure. Either we've got more payload to decode,
  // or we've run out because the payload is too short, in which case
  // OnFrameSizeError will have already been called by the
  // QuicHttpFrameDecoderState.
  DCHECK((status == QuicHttpDecodeStatus::kDecodeInProgress &&
          state->remaining_payload() > 0) ||
         (status == QuicHttpDecodeStatus::kDecodeError &&
          state->remaining_payload() == 0))
      << "\n status=" << status
      << "; remaining_payload=" << state->remaining_payload();
  return status;
}

}  // namespace net
