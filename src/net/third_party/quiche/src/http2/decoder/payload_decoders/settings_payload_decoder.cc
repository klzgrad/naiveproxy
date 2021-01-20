// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/third_party/quiche/src/http2/decoder/payload_decoders/settings_payload_decoder.h"

#include "net/third_party/quiche/src/http2/decoder/decode_buffer.h"
#include "net/third_party/quiche/src/http2/decoder/http2_frame_decoder_listener.h"
#include "net/third_party/quiche/src/http2/http2_constants.h"
#include "net/third_party/quiche/src/http2/http2_structures.h"
#include "net/third_party/quiche/src/http2/platform/api/http2_logging.h"

namespace http2 {

DecodeStatus SettingsPayloadDecoder::StartDecodingPayload(
    FrameDecoderState* state,
    DecodeBuffer* db) {
  const Http2FrameHeader& frame_header = state->frame_header();
  const uint32_t total_length = frame_header.payload_length;

  HTTP2_DVLOG(2) << "SettingsPayloadDecoder::StartDecodingPayload: "
                 << frame_header;
  DCHECK_EQ(Http2FrameType::SETTINGS, frame_header.type);
  DCHECK_LE(db->Remaining(), total_length);
  DCHECK_EQ(0, frame_header.flags & ~(Http2FrameFlag::ACK));

  if (frame_header.IsAck()) {
    if (total_length == 0) {
      state->listener()->OnSettingsAck(frame_header);
      return DecodeStatus::kDecodeDone;
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

DecodeStatus SettingsPayloadDecoder::ResumeDecodingPayload(
    FrameDecoderState* state,
    DecodeBuffer* db) {
  HTTP2_DVLOG(2) << "SettingsPayloadDecoder::ResumeDecodingPayload"
                 << "  remaining_payload=" << state->remaining_payload()
                 << "  db->Remaining=" << db->Remaining();
  DCHECK_EQ(Http2FrameType::SETTINGS, state->frame_header().type);
  DCHECK_LE(db->Remaining(), state->frame_header().payload_length);

  DecodeStatus status =
      state->ResumeDecodingStructureInPayload(&setting_fields_, db);
  if (status == DecodeStatus::kDecodeDone) {
    state->listener()->OnSetting(setting_fields_);
    return StartDecodingSettings(state, db);
  }
  return HandleNotDone(state, db, status);
}

DecodeStatus SettingsPayloadDecoder::StartDecodingSettings(
    FrameDecoderState* state,
    DecodeBuffer* db) {
  HTTP2_DVLOG(2) << "SettingsPayloadDecoder::StartDecodingSettings"
                 << "  remaining_payload=" << state->remaining_payload()
                 << "  db->Remaining=" << db->Remaining();
  while (state->remaining_payload() > 0) {
    DecodeStatus status =
        state->StartDecodingStructureInPayload(&setting_fields_, db);
    if (status == DecodeStatus::kDecodeDone) {
      state->listener()->OnSetting(setting_fields_);
      continue;
    }
    return HandleNotDone(state, db, status);
  }
  HTTP2_DVLOG(2) << "LEAVING SettingsPayloadDecoder::StartDecodingSettings"
                 << "\n\tdb->Remaining=" << db->Remaining()
                 << "\n\t remaining_payload=" << state->remaining_payload();
  state->listener()->OnSettingsEnd();
  return DecodeStatus::kDecodeDone;
}

DecodeStatus SettingsPayloadDecoder::HandleNotDone(FrameDecoderState* state,
                                                   DecodeBuffer* db,
                                                   DecodeStatus status) {
  // Not done decoding the structure. Either we've got more payload to decode,
  // or we've run out because the payload is too short, in which case
  // OnFrameSizeError will have already been called.
  DCHECK(
      (status == DecodeStatus::kDecodeInProgress &&
       state->remaining_payload() > 0) ||
      (status == DecodeStatus::kDecodeError && state->remaining_payload() == 0))
      << "\n status=" << status
      << "; remaining_payload=" << state->remaining_payload()
      << "; db->Remaining=" << db->Remaining();
  return status;
}

}  // namespace http2
