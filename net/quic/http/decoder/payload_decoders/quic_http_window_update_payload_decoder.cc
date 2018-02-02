// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/quic/http/decoder/payload_decoders/quic_http_window_update_payload_decoder.h"

#include <cstdint>

#include "base/logging.h"
#include "net/quic/http/decoder/quic_http_decode_buffer.h"
#include "net/quic/http/decoder/quic_http_decode_structures.h"
#include "net/quic/http/decoder/quic_http_frame_decoder_listener.h"
#include "net/quic/http/quic_http_constants.h"
#include "net/quic/http/quic_http_structures.h"

namespace net {

QuicHttpDecodeStatus
QuicHttpWindowUpdateQuicHttpPayloadDecoder::StartDecodingPayload(
    QuicHttpFrameDecoderState* state,
    QuicHttpDecodeBuffer* db) {
  const QuicHttpFrameHeader& frame_header = state->frame_header();
  const uint32_t total_length = frame_header.payload_length;

  DVLOG(2)
      << "QuicHttpWindowUpdateQuicHttpPayloadDecoder::StartDecodingPayload: "
      << frame_header;

  DCHECK_EQ(QuicHttpFrameType::WINDOW_UPDATE, frame_header.type);
  DCHECK_LE(db->Remaining(), total_length);

  // WINDOW_UPDATE frames have no flags.
  DCHECK_EQ(0, frame_header.flags);

  // Special case for when the payload is the correct size and entirely in
  // the buffer.
  if (db->Remaining() == QuicHttpWindowUpdateFields::EncodedSize() &&
      total_length == QuicHttpWindowUpdateFields::EncodedSize()) {
    DoDecode(&window_update_fields_, db);
    state->listener()->OnWindowUpdate(
        frame_header, window_update_fields_.window_size_increment);
    return QuicHttpDecodeStatus::kDecodeDone;
  }
  state->InitializeRemainders();
  return HandleStatus(state, state->StartDecodingStructureInPayload(
                                 &window_update_fields_, db));
}

QuicHttpDecodeStatus
QuicHttpWindowUpdateQuicHttpPayloadDecoder::ResumeDecodingPayload(
    QuicHttpFrameDecoderState* state,
    QuicHttpDecodeBuffer* db) {
  DVLOG(2) << "ResumeDecodingPayload: remaining_payload="
           << state->remaining_payload()
           << "; db->Remaining=" << db->Remaining();
  DCHECK_EQ(QuicHttpFrameType::WINDOW_UPDATE, state->frame_header().type);
  DCHECK_LE(db->Remaining(), state->frame_header().payload_length);
  return HandleStatus(state, state->ResumeDecodingStructureInPayload(
                                 &window_update_fields_, db));
}

QuicHttpDecodeStatus QuicHttpWindowUpdateQuicHttpPayloadDecoder::HandleStatus(
    QuicHttpFrameDecoderState* state,
    QuicHttpDecodeStatus status) {
  DVLOG(2) << "HandleStatus: status=" << status
           << "; remaining_payload=" << state->remaining_payload();
  if (status == QuicHttpDecodeStatus::kDecodeDone) {
    if (state->remaining_payload() == 0) {
      state->listener()->OnWindowUpdate(
          state->frame_header(), window_update_fields_.window_size_increment);
      return QuicHttpDecodeStatus::kDecodeDone;
    }
    // Payload is too long.
    return state->ReportFrameSizeError();
  }
  // Not done decoding the structure. Either we've got more payload to decode,
  // or we've run out because the payload is too short, in which case
  // OnFrameSizeError will have already been called.
  DCHECK((status == QuicHttpDecodeStatus::kDecodeInProgress &&
          state->remaining_payload() > 0) ||
         (status == QuicHttpDecodeStatus::kDecodeError &&
          state->remaining_payload() == 0))
      << "\n status=" << status
      << "; remaining_payload=" << state->remaining_payload();
  return status;
}

}  // namespace net
