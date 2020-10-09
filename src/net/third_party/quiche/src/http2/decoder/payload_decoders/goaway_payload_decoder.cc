// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/third_party/quiche/src/http2/decoder/payload_decoders/goaway_payload_decoder.h"

#include <stddef.h>

#include "net/third_party/quiche/src/http2/decoder/decode_buffer.h"
#include "net/third_party/quiche/src/http2/decoder/http2_frame_decoder_listener.h"
#include "net/third_party/quiche/src/http2/http2_constants.h"
#include "net/third_party/quiche/src/http2/http2_structures.h"
#include "net/third_party/quiche/src/http2/platform/api/http2_bug_tracker.h"
#include "net/third_party/quiche/src/http2/platform/api/http2_logging.h"
#include "net/third_party/quiche/src/http2/platform/api/http2_macros.h"

namespace http2 {

std::ostream& operator<<(std::ostream& out,
                         GoAwayPayloadDecoder::PayloadState v) {
  switch (v) {
    case GoAwayPayloadDecoder::PayloadState::kStartDecodingFixedFields:
      return out << "kStartDecodingFixedFields";
    case GoAwayPayloadDecoder::PayloadState::kHandleFixedFieldsStatus:
      return out << "kHandleFixedFieldsStatus";
    case GoAwayPayloadDecoder::PayloadState::kReadOpaqueData:
      return out << "kReadOpaqueData";
    case GoAwayPayloadDecoder::PayloadState::kResumeDecodingFixedFields:
      return out << "kResumeDecodingFixedFields";
  }
  // Since the value doesn't come over the wire, only a programming bug should
  // result in reaching this point.
  int unknown = static_cast<int>(v);
  HTTP2_BUG << "Invalid GoAwayPayloadDecoder::PayloadState: " << unknown;
  return out << "GoAwayPayloadDecoder::PayloadState(" << unknown << ")";
}

DecodeStatus GoAwayPayloadDecoder::StartDecodingPayload(
    FrameDecoderState* state,
    DecodeBuffer* db) {
  HTTP2_DVLOG(2) << "GoAwayPayloadDecoder::StartDecodingPayload: "
                 << state->frame_header();
  DCHECK_EQ(Http2FrameType::GOAWAY, state->frame_header().type);
  DCHECK_LE(db->Remaining(), state->frame_header().payload_length);
  DCHECK_EQ(0, state->frame_header().flags);

  state->InitializeRemainders();
  payload_state_ = PayloadState::kStartDecodingFixedFields;
  return ResumeDecodingPayload(state, db);
}

DecodeStatus GoAwayPayloadDecoder::ResumeDecodingPayload(
    FrameDecoderState* state,
    DecodeBuffer* db) {
  HTTP2_DVLOG(2)
      << "GoAwayPayloadDecoder::ResumeDecodingPayload: remaining_payload="
      << state->remaining_payload() << ", db->Remaining=" << db->Remaining();

  const Http2FrameHeader& frame_header = state->frame_header();
  DCHECK_EQ(Http2FrameType::GOAWAY, frame_header.type);
  DCHECK_LE(db->Remaining(), frame_header.payload_length);
  DCHECK_NE(PayloadState::kHandleFixedFieldsStatus, payload_state_);

  // |status| has to be initialized to some value to avoid compiler error in
  // case PayloadState::kHandleFixedFieldsStatus below, but value does not
  // matter, see DCHECK_NE above.
  DecodeStatus status = DecodeStatus::kDecodeError;
  size_t avail;
  while (true) {
    HTTP2_DVLOG(2)
        << "GoAwayPayloadDecoder::ResumeDecodingPayload payload_state_="
        << payload_state_;
    switch (payload_state_) {
      case PayloadState::kStartDecodingFixedFields:
        status = state->StartDecodingStructureInPayload(&goaway_fields_, db);
        HTTP2_FALLTHROUGH;

      case PayloadState::kHandleFixedFieldsStatus:
        if (status == DecodeStatus::kDecodeDone) {
          state->listener()->OnGoAwayStart(frame_header, goaway_fields_);
        } else {
          // Not done decoding the structure. Either we've got more payload
          // to decode, or we've run out because the payload is too short,
          // in which case OnFrameSizeError will have already been called.
          DCHECK((status == DecodeStatus::kDecodeInProgress &&
                  state->remaining_payload() > 0) ||
                 (status == DecodeStatus::kDecodeError &&
                  state->remaining_payload() == 0))
              << "\n status=" << status
              << "; remaining_payload=" << state->remaining_payload();
          payload_state_ = PayloadState::kResumeDecodingFixedFields;
          return status;
        }
        HTTP2_FALLTHROUGH;

      case PayloadState::kReadOpaqueData:
        // The opaque data is all the remains to be decoded, so anything left
        // in the decode buffer is opaque data.
        avail = db->Remaining();
        if (avail > 0) {
          state->listener()->OnGoAwayOpaqueData(db->cursor(), avail);
          db->AdvanceCursor(avail);
          state->ConsumePayload(avail);
        }
        if (state->remaining_payload() > 0) {
          payload_state_ = PayloadState::kReadOpaqueData;
          return DecodeStatus::kDecodeInProgress;
        }
        state->listener()->OnGoAwayEnd();
        return DecodeStatus::kDecodeDone;

      case PayloadState::kResumeDecodingFixedFields:
        status = state->ResumeDecodingStructureInPayload(&goaway_fields_, db);
        payload_state_ = PayloadState::kHandleFixedFieldsStatus;
        continue;
    }
    HTTP2_BUG << "PayloadState: " << payload_state_;
  }
}

}  // namespace http2
