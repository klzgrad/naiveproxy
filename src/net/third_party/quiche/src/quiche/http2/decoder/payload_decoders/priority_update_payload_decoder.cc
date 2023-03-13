// Copyright 2020 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "quiche/http2/decoder/payload_decoders/priority_update_payload_decoder.h"

#include <stddef.h>

#include "absl/base/macros.h"
#include "quiche/http2/decoder/decode_buffer.h"
#include "quiche/http2/decoder/http2_frame_decoder_listener.h"
#include "quiche/http2/http2_constants.h"
#include "quiche/http2/http2_structures.h"
#include "quiche/common/platform/api/quiche_bug_tracker.h"
#include "quiche/common/platform/api/quiche_logging.h"

namespace http2 {

std::ostream& operator<<(std::ostream& out,
                         PriorityUpdatePayloadDecoder::PayloadState v) {
  switch (v) {
    case PriorityUpdatePayloadDecoder::PayloadState::kStartDecodingFixedFields:
      return out << "kStartDecodingFixedFields";
    case PriorityUpdatePayloadDecoder::PayloadState::kResumeDecodingFixedFields:
      return out << "kResumeDecodingFixedFields";
    case PriorityUpdatePayloadDecoder::PayloadState::kHandleFixedFieldsStatus:
      return out << "kHandleFixedFieldsStatus";
    case PriorityUpdatePayloadDecoder::PayloadState::kReadPriorityFieldValue:
      return out << "kReadPriorityFieldValue";
  }
  // Since the value doesn't come over the wire, only a programming bug should
  // result in reaching this point.
  int unknown = static_cast<int>(v);
  QUICHE_BUG(http2_bug_173_1)
      << "Invalid PriorityUpdatePayloadDecoder::PayloadState: " << unknown;
  return out << "PriorityUpdatePayloadDecoder::PayloadState(" << unknown << ")";
}

DecodeStatus PriorityUpdatePayloadDecoder::StartDecodingPayload(
    FrameDecoderState* state, DecodeBuffer* db) {
  QUICHE_DVLOG(2) << "PriorityUpdatePayloadDecoder::StartDecodingPayload: "
                  << state->frame_header();
  QUICHE_DCHECK_EQ(Http2FrameType::PRIORITY_UPDATE, state->frame_header().type);
  QUICHE_DCHECK_LE(db->Remaining(), state->frame_header().payload_length);
  QUICHE_DCHECK_EQ(0, state->frame_header().flags);

  state->InitializeRemainders();
  payload_state_ = PayloadState::kStartDecodingFixedFields;
  return ResumeDecodingPayload(state, db);
}

DecodeStatus PriorityUpdatePayloadDecoder::ResumeDecodingPayload(
    FrameDecoderState* state, DecodeBuffer* db) {
  QUICHE_DVLOG(2) << "PriorityUpdatePayloadDecoder::ResumeDecodingPayload: "
                     "remaining_payload="
                  << state->remaining_payload()
                  << ", db->Remaining=" << db->Remaining();

  const Http2FrameHeader& frame_header = state->frame_header();
  QUICHE_DCHECK_EQ(Http2FrameType::PRIORITY_UPDATE, frame_header.type);
  QUICHE_DCHECK_LE(db->Remaining(), frame_header.payload_length);
  QUICHE_DCHECK_NE(PayloadState::kHandleFixedFieldsStatus, payload_state_);

  // |status| has to be initialized to some value to avoid compiler error in
  // case PayloadState::kHandleFixedFieldsStatus below, but value does not
  // matter, see QUICHE_DCHECK_NE above.
  DecodeStatus status = DecodeStatus::kDecodeError;
  size_t avail;
  while (true) {
    QUICHE_DVLOG(2)
        << "PriorityUpdatePayloadDecoder::ResumeDecodingPayload payload_state_="
        << payload_state_;
    switch (payload_state_) {
      case PayloadState::kStartDecodingFixedFields:
        status = state->StartDecodingStructureInPayload(
            &priority_update_fields_, db);
        ABSL_FALLTHROUGH_INTENDED;

      case PayloadState::kHandleFixedFieldsStatus:
        if (status == DecodeStatus::kDecodeDone) {
          state->listener()->OnPriorityUpdateStart(frame_header,
                                                   priority_update_fields_);
        } else {
          // Not done decoding the structure. Either we've got more payload
          // to decode, or we've run out because the payload is too short,
          // in which case OnFrameSizeError will have already been called.
          QUICHE_DCHECK((status == DecodeStatus::kDecodeInProgress &&
                         state->remaining_payload() > 0) ||
                        (status == DecodeStatus::kDecodeError &&
                         state->remaining_payload() == 0))
              << "\n status=" << status
              << "; remaining_payload=" << state->remaining_payload();
          payload_state_ = PayloadState::kResumeDecodingFixedFields;
          return status;
        }
        ABSL_FALLTHROUGH_INTENDED;

      case PayloadState::kReadPriorityFieldValue:
        // Anything left in the decode buffer is the Priority Field Value.
        avail = db->Remaining();
        if (avail > 0) {
          state->listener()->OnPriorityUpdatePayload(db->cursor(), avail);
          db->AdvanceCursor(avail);
          state->ConsumePayload(avail);
        }
        if (state->remaining_payload() > 0) {
          payload_state_ = PayloadState::kReadPriorityFieldValue;
          return DecodeStatus::kDecodeInProgress;
        }
        state->listener()->OnPriorityUpdateEnd();
        return DecodeStatus::kDecodeDone;

      case PayloadState::kResumeDecodingFixedFields:
        status = state->ResumeDecodingStructureInPayload(
            &priority_update_fields_, db);
        payload_state_ = PayloadState::kHandleFixedFieldsStatus;
        continue;
    }
    QUICHE_BUG(http2_bug_173_2) << "PayloadState: " << payload_state_;
  }
}

}  // namespace http2
