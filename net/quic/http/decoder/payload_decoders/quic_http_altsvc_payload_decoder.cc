// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/quic/http/decoder/payload_decoders/quic_http_altsvc_payload_decoder.h"

#include <stddef.h>

#include "base/logging.h"
#include "base/macros.h"
#include "net/quic/http/decoder/quic_http_decode_buffer.h"
#include "net/quic/http/decoder/quic_http_frame_decoder_listener.h"
#include "net/quic/http/quic_http_constants.h"
#include "net/quic/http/quic_http_structures.h"
#include "net/quic/platform/api/quic_bug_tracker.h"

namespace net {

std::ostream& operator<<(std::ostream& out,
                         QuicHttpAltSvcQuicHttpPayloadDecoder::PayloadState v) {
  switch (v) {
    case QuicHttpAltSvcQuicHttpPayloadDecoder::PayloadState::
        kStartDecodingStruct:
      return out << "kStartDecodingStruct";
    case QuicHttpAltSvcQuicHttpPayloadDecoder::PayloadState::
        kMaybeDecodedStruct:
      return out << "kMaybeDecodedStruct";
    case QuicHttpAltSvcQuicHttpPayloadDecoder::PayloadState::kDecodingStrings:
      return out << "kDecodingStrings";
    case QuicHttpAltSvcQuicHttpPayloadDecoder::PayloadState::
        kResumeDecodingStruct:
      return out << "kResumeDecodingStruct";
  }
  // Since the value doesn't come over the wire, only a programming bug should
  // result in reaching this point.
  int unknown = static_cast<int>(v);
  QUIC_BUG << "Invalid QuicHttpAltSvcQuicHttpPayloadDecoder::PayloadState: "
           << unknown;
  return out << "QuicHttpAltSvcQuicHttpPayloadDecoder::PayloadState(" << unknown
             << ")";
}

QuicHttpDecodeStatus QuicHttpAltSvcQuicHttpPayloadDecoder::StartDecodingPayload(
    QuicHttpFrameDecoderState* state,
    QuicHttpDecodeBuffer* db) {
  DVLOG(2) << "QuicHttpAltSvcQuicHttpPayloadDecoder::StartDecodingPayload: "
           << state->frame_header();
  DCHECK_EQ(QuicHttpFrameType::ALTSVC, state->frame_header().type);
  DCHECK_LE(db->Remaining(), state->frame_header().payload_length);
  DCHECK_EQ(0, state->frame_header().flags);

  state->InitializeRemainders();
  payload_state_ = PayloadState::kStartDecodingStruct;

  return ResumeDecodingPayload(state, db);
}

QuicHttpDecodeStatus
QuicHttpAltSvcQuicHttpPayloadDecoder::ResumeDecodingPayload(
    QuicHttpFrameDecoderState* state,
    QuicHttpDecodeBuffer* db) {
  const QuicHttpFrameHeader& frame_header = state->frame_header();
  DVLOG(2) << "QuicHttpAltSvcQuicHttpPayloadDecoder::ResumeDecodingPayload: "
           << frame_header;
  DCHECK_EQ(QuicHttpFrameType::ALTSVC, frame_header.type);
  DCHECK_LE(state->remaining_payload(), frame_header.payload_length);
  DCHECK_LE(db->Remaining(), state->remaining_payload());
  DCHECK_NE(PayloadState::kMaybeDecodedStruct, payload_state_);
  QuicHttpDecodeStatus status = QuicHttpDecodeStatus::kDecodeError;
  while (true) {
    DVLOG(2) << "QuicHttpAltSvcQuicHttpPayloadDecoder::ResumeDecodingPayload "
                "payload_state_="
             << payload_state_;
    switch (payload_state_) {
      case PayloadState::kStartDecodingStruct:
        status = state->StartDecodingStructureInPayload(&altsvc_fields_, db);
      // FALLTHROUGH_INTENDED;

      case PayloadState::kMaybeDecodedStruct:
        if (status == QuicHttpDecodeStatus::kDecodeDone &&
            altsvc_fields_.origin_length <= state->remaining_payload()) {
          size_t origin_length = altsvc_fields_.origin_length;
          size_t value_length = state->remaining_payload() - origin_length;
          state->listener()->OnAltSvcStart(frame_header, origin_length,
                                           value_length);
        } else if (status != QuicHttpDecodeStatus::kDecodeDone) {
          DCHECK(state->remaining_payload() > 0 ||
                 status == QuicHttpDecodeStatus::kDecodeError)
              << "\nremaining_payload: " << state->remaining_payload()
              << "\nstatus: " << status << "\nheader: " << frame_header;
          // Assume in progress.
          payload_state_ = PayloadState::kResumeDecodingStruct;
          return status;
        } else {
          // The origin's length is longer than the remaining payload.
          DCHECK_GT(altsvc_fields_.origin_length, state->remaining_payload());
          return state->ReportFrameSizeError();
        }
      // FALLTHROUGH_INTENDED;

      case PayloadState::kDecodingStrings:
        return DecodeStrings(state, db);

      case PayloadState::kResumeDecodingStruct:
        status = state->ResumeDecodingStructureInPayload(&altsvc_fields_, db);
        payload_state_ = PayloadState::kMaybeDecodedStruct;
        continue;
    }
    QUIC_BUG << "PayloadState: " << payload_state_;
  }
}

QuicHttpDecodeStatus QuicHttpAltSvcQuicHttpPayloadDecoder::DecodeStrings(
    QuicHttpFrameDecoderState* state,
    QuicHttpDecodeBuffer* db) {
  DVLOG(2) << "QuicHttpAltSvcQuicHttpPayloadDecoder::DecodeStrings "
              "remaining_payload="
           << state->remaining_payload()
           << ", db->Remaining=" << db->Remaining();
  // Note that we don't explicitly keep track of exactly how far through the
  // origin; instead we compute it from how much is left of the original
  // payload length and the decoded total length of the origin.
  size_t origin_length = altsvc_fields_.origin_length;
  size_t value_length = state->frame_header().payload_length - origin_length -
                        QuicHttpAltSvcFields::EncodedSize();
  if (state->remaining_payload() > value_length) {
    size_t remaining_origin_length = state->remaining_payload() - value_length;
    size_t avail = db->MinLengthRemaining(remaining_origin_length);
    state->listener()->OnAltSvcOriginData(db->cursor(), avail);
    db->AdvanceCursor(avail);
    state->ConsumePayload(avail);
    if (remaining_origin_length > avail) {
      payload_state_ = PayloadState::kDecodingStrings;
      return QuicHttpDecodeStatus::kDecodeInProgress;
    }
  }
  // All that is left is the value string.
  DCHECK_LE(state->remaining_payload(), value_length);
  DCHECK_LE(db->Remaining(), state->remaining_payload());
  if (db->HasData()) {
    size_t avail = db->Remaining();
    state->listener()->OnAltSvcValueData(db->cursor(), avail);
    db->AdvanceCursor(avail);
    state->ConsumePayload(avail);
  }
  if (state->remaining_payload() == 0) {
    state->listener()->OnAltSvcEnd();
    return QuicHttpDecodeStatus::kDecodeDone;
  }
  payload_state_ = PayloadState::kDecodingStrings;
  return QuicHttpDecodeStatus::kDecodeInProgress;
}

}  // namespace net
