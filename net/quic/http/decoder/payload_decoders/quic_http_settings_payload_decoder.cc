// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/quic/http/decoder/payload_decoders/quic_http_settings_payload_decoder.h"

#include <cstdint>

#include "base/logging.h"
#include "net/quic/http/decoder/quic_http_decode_buffer.h"
#include "net/quic/http/decoder/quic_http_frame_decoder_listener.h"
#include "net/quic/http/quic_http_constants.h"
#include "net/quic/http/quic_http_structures.h"

namespace net {

QuicHttpDecodeStatus
QuicHttpQuicHttpSettingsQuicHttpPayloadDecoder::StartDecodingPayload(
    QuicHttpFrameDecoderState* state,
    QuicHttpDecodeBuffer* db) {
  const QuicHttpFrameHeader& frame_header = state->frame_header();
  const uint32_t total_length = frame_header.payload_length;

  DVLOG(2) << "QuicHttpQuicHttpSettingsQuicHttpPayloadDecoder::"
              "StartDecodingPayload: "
           << frame_header;
  DCHECK_EQ(QuicHttpFrameType::SETTINGS, frame_header.type);
  DCHECK_LE(db->Remaining(), total_length);
  DCHECK_EQ(0, frame_header.flags & ~(QuicHttpFrameFlag::QUIC_HTTP_ACK));

  if (frame_header.IsAck()) {
    if (total_length == 0) {
      state->listener()->OnSettingsAck(frame_header);
      return QuicHttpDecodeStatus::kDecodeDone;
    } else {
      state->InitializeRemainders();
      return state->ReportFrameSizeError();
    }
  } else {
    state->InitializeRemainders();
    state->listener()->OnSettingsStart(frame_header);
    return StartDecodingSettings(state, db);
  }
}

QuicHttpDecodeStatus
QuicHttpQuicHttpSettingsQuicHttpPayloadDecoder::ResumeDecodingPayload(
    QuicHttpFrameDecoderState* state,
    QuicHttpDecodeBuffer* db) {
  DVLOG(2)
      << "QuicHttpQuicHttpSettingsQuicHttpPayloadDecoder::ResumeDecodingPayload"
      << "  remaining_payload=" << state->remaining_payload()
      << "  db->Remaining=" << db->Remaining();
  DCHECK_EQ(QuicHttpFrameType::SETTINGS, state->frame_header().type);
  DCHECK_LE(db->Remaining(), state->frame_header().payload_length);

  QuicHttpDecodeStatus status =
      state->ResumeDecodingStructureInPayload(&setting_fields_, db);
  if (status == QuicHttpDecodeStatus::kDecodeDone) {
    state->listener()->OnSetting(setting_fields_);
    return StartDecodingSettings(state, db);
  }
  return HandleNotDone(state, db, status);
}

QuicHttpDecodeStatus
QuicHttpQuicHttpSettingsQuicHttpPayloadDecoder::StartDecodingSettings(
    QuicHttpFrameDecoderState* state,
    QuicHttpDecodeBuffer* db) {
  DVLOG(2)
      << "QuicHttpQuicHttpSettingsQuicHttpPayloadDecoder::StartDecodingSettings"
      << "  remaining_payload=" << state->remaining_payload()
      << "  db->Remaining=" << db->Remaining();
  while (state->remaining_payload() > 0) {
    QuicHttpDecodeStatus status =
        state->StartDecodingStructureInPayload(&setting_fields_, db);
    if (status == QuicHttpDecodeStatus::kDecodeDone) {
      state->listener()->OnSetting(setting_fields_);
      continue;
    }
    return HandleNotDone(state, db, status);
  }
  DVLOG(2) << "LEAVING "
              "QuicHttpQuicHttpSettingsQuicHttpPayloadDecoder::"
              "StartDecodingSettings"
           << "\n\tdb->Remaining=" << db->Remaining()
           << "\n\t remaining_payload=" << state->remaining_payload();
  state->listener()->OnSettingsEnd();
  return QuicHttpDecodeStatus::kDecodeDone;
}

QuicHttpDecodeStatus
QuicHttpQuicHttpSettingsQuicHttpPayloadDecoder::HandleNotDone(
    QuicHttpFrameDecoderState* state,
    QuicHttpDecodeBuffer* db,
    QuicHttpDecodeStatus status) {
  // Not done decoding the structure. Either we've got more payload to decode,
  // or we've run out because the payload is too short, in which case
  // OnFrameSizeError will have already been called.
  DCHECK((status == QuicHttpDecodeStatus::kDecodeInProgress &&
          state->remaining_payload() > 0) ||
         (status == QuicHttpDecodeStatus::kDecodeError &&
          state->remaining_payload() == 0))
      << "\n status=" << status
      << "; remaining_payload=" << state->remaining_payload()
      << "; db->Remaining=" << db->Remaining();
  return status;
}

}  // namespace net
