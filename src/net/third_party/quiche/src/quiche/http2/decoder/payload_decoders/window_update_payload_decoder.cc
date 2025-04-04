// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "quiche/http2/decoder/payload_decoders/window_update_payload_decoder.h"

#include "quiche/http2/core/http2_constants.h"
#include "quiche/http2/core/http2_structures.h"
#include "quiche/http2/decoder/decode_buffer.h"
#include "quiche/http2/decoder/decode_http2_structures.h"
#include "quiche/http2/decoder/http2_frame_decoder_listener.h"
#include "quiche/common/platform/api/quiche_logging.h"

namespace http2 {

DecodeStatus WindowUpdatePayloadDecoder::StartDecodingPayload(
    FrameDecoderState* state, DecodeBuffer* db) {
  const Http2FrameHeader& frame_header = state->frame_header();
  const uint32_t total_length = frame_header.payload_length;

  QUICHE_DVLOG(2) << "WindowUpdatePayloadDecoder::StartDecodingPayload: "
                  << frame_header;

  QUICHE_DCHECK_EQ(Http2FrameType::WINDOW_UPDATE, frame_header.type);
  QUICHE_DCHECK_LE(db->Remaining(), total_length);

  // WINDOW_UPDATE frames have no flags.
  QUICHE_DCHECK_EQ(0, frame_header.flags);

  // Special case for when the payload is the correct size and entirely in
  // the buffer.
  if (db->Remaining() == Http2WindowUpdateFields::EncodedSize() &&
      total_length == Http2WindowUpdateFields::EncodedSize()) {
    DoDecode(&window_update_fields_, db);
    state->listener()->OnWindowUpdate(
        frame_header, window_update_fields_.window_size_increment);
    return DecodeStatus::kDecodeDone;
  }
  state->InitializeRemainders();
  return HandleStatus(state, state->StartDecodingStructureInPayload(
                                 &window_update_fields_, db));
}

DecodeStatus WindowUpdatePayloadDecoder::ResumeDecodingPayload(
    FrameDecoderState* state, DecodeBuffer* db) {
  QUICHE_DVLOG(2) << "ResumeDecodingPayload: remaining_payload="
                  << state->remaining_payload()
                  << "; db->Remaining=" << db->Remaining();
  QUICHE_DCHECK_EQ(Http2FrameType::WINDOW_UPDATE, state->frame_header().type);
  QUICHE_DCHECK_LE(db->Remaining(), state->frame_header().payload_length);
  return HandleStatus(state, state->ResumeDecodingStructureInPayload(
                                 &window_update_fields_, db));
}

DecodeStatus WindowUpdatePayloadDecoder::HandleStatus(FrameDecoderState* state,
                                                      DecodeStatus status) {
  QUICHE_DVLOG(2) << "HandleStatus: status=" << status
                  << "; remaining_payload=" << state->remaining_payload();
  if (status == DecodeStatus::kDecodeDone) {
    if (state->remaining_payload() == 0) {
      state->listener()->OnWindowUpdate(
          state->frame_header(), window_update_fields_.window_size_increment);
      return DecodeStatus::kDecodeDone;
    }
    // Payload is too long.
    return state->ReportFrameSizeError();
  }
  // Not done decoding the structure. Either we've got more payload to decode,
  // or we've run out because the payload is too short, in which case
  // OnFrameSizeError will have already been called.
  QUICHE_DCHECK(
      (status == DecodeStatus::kDecodeInProgress &&
       state->remaining_payload() > 0) ||
      (status == DecodeStatus::kDecodeError && state->remaining_payload() == 0))
      << "\n status=" << status
      << "; remaining_payload=" << state->remaining_payload();
  return status;
}

}  // namespace http2
