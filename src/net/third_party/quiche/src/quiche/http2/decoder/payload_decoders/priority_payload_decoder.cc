// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "quiche/http2/decoder/payload_decoders/priority_payload_decoder.h"

#include "quiche/http2/decoder/decode_buffer.h"
#include "quiche/http2/decoder/http2_frame_decoder_listener.h"
#include "quiche/http2/http2_constants.h"
#include "quiche/http2/http2_structures.h"
#include "quiche/common/platform/api/quiche_logging.h"

namespace http2 {

DecodeStatus PriorityPayloadDecoder::StartDecodingPayload(
    FrameDecoderState* state, DecodeBuffer* db) {
  QUICHE_DVLOG(2) << "PriorityPayloadDecoder::StartDecodingPayload: "
                  << state->frame_header();
  QUICHE_DCHECK_EQ(Http2FrameType::PRIORITY, state->frame_header().type);
  QUICHE_DCHECK_LE(db->Remaining(), state->frame_header().payload_length);
  // PRIORITY frames have no flags.
  QUICHE_DCHECK_EQ(0, state->frame_header().flags);
  state->InitializeRemainders();
  return HandleStatus(
      state, state->StartDecodingStructureInPayload(&priority_fields_, db));
}

DecodeStatus PriorityPayloadDecoder::ResumeDecodingPayload(
    FrameDecoderState* state, DecodeBuffer* db) {
  QUICHE_DVLOG(2) << "PriorityPayloadDecoder::ResumeDecodingPayload"
                  << "  remaining_payload=" << state->remaining_payload()
                  << "  db->Remaining=" << db->Remaining();
  QUICHE_DCHECK_EQ(Http2FrameType::PRIORITY, state->frame_header().type);
  QUICHE_DCHECK_LE(db->Remaining(), state->frame_header().payload_length);
  return HandleStatus(
      state, state->ResumeDecodingStructureInPayload(&priority_fields_, db));
}

DecodeStatus PriorityPayloadDecoder::HandleStatus(FrameDecoderState* state,
                                                  DecodeStatus status) {
  if (status == DecodeStatus::kDecodeDone) {
    if (state->remaining_payload() == 0) {
      state->listener()->OnPriorityFrame(state->frame_header(),
                                         priority_fields_);
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
