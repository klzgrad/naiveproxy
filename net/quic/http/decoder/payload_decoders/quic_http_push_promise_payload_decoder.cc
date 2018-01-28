// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/quic/http/decoder/payload_decoders/quic_http_push_promise_payload_decoder.h"

#include <stddef.h>

#include <cstdint>

#include "base/logging.h"
#include "base/macros.h"
#include "net/quic/http/decoder/quic_http_decode_buffer.h"
#include "net/quic/http/decoder/quic_http_frame_decoder_listener.h"
#include "net/quic/http/quic_http_constants.h"
#include "net/quic/http/quic_http_structures.h"
#include "net/quic/platform/api/quic_bug_tracker.h"

namespace net {

std::ostream& operator<<(
    std::ostream& out,
    QuicHttpPushPromiseQuicHttpPayloadDecoder::PayloadState v) {
  switch (v) {
    case QuicHttpPushPromiseQuicHttpPayloadDecoder::PayloadState::
        kReadPadLength:
      return out << "kReadPadLength";
    case QuicHttpPushPromiseQuicHttpPayloadDecoder::PayloadState::
        kStartDecodingPushPromiseFields:
      return out << "kStartDecodingPushPromiseFields";
    case QuicHttpPushPromiseQuicHttpPayloadDecoder::PayloadState::kReadPayload:
      return out << "kReadPayload";
    case QuicHttpPushPromiseQuicHttpPayloadDecoder::PayloadState::kSkipPadding:
      return out << "kSkipPadding";
    case QuicHttpPushPromiseQuicHttpPayloadDecoder::PayloadState::
        kResumeDecodingPushPromiseFields:
      return out << "kResumeDecodingPushPromiseFields";
  }
  return out << static_cast<int>(v);
}

QuicHttpDecodeStatus
QuicHttpPushPromiseQuicHttpPayloadDecoder::StartDecodingPayload(
    QuicHttpFrameDecoderState* state,
    QuicHttpDecodeBuffer* db) {
  const QuicHttpFrameHeader& frame_header = state->frame_header();
  const uint32_t total_length = frame_header.payload_length;

  DVLOG(2)
      << "QuicHttpPushPromiseQuicHttpPayloadDecoder::StartDecodingPayload: "
      << frame_header;

  DCHECK_EQ(QuicHttpFrameType::PUSH_PROMISE, frame_header.type);
  DCHECK_LE(db->Remaining(), total_length);
  DCHECK_EQ(0, frame_header.flags & ~(QuicHttpFrameFlag::QUIC_HTTP_END_HEADERS |
                                      QuicHttpFrameFlag::QUIC_HTTP_PADDED));

  if (!frame_header.IsPadded()) {
    // If it turns out that PUSH_PROMISE frames without padding are sufficiently
    // common, and that they are usually short enough that they fit entirely
    // into one QuicHttpDecodeBuffer, we can detect that here and implement a
    // special case, avoiding the state machine in ResumeDecodingPayload.
    payload_state_ = PayloadState::kStartDecodingPushPromiseFields;
  } else {
    payload_state_ = PayloadState::kReadPadLength;
  }
  state->InitializeRemainders();
  return ResumeDecodingPayload(state, db);
}

QuicHttpDecodeStatus
QuicHttpPushPromiseQuicHttpPayloadDecoder::ResumeDecodingPayload(
    QuicHttpFrameDecoderState* state,
    QuicHttpDecodeBuffer* db) {
  DVLOG(2) << "QuicHttpUnknownQuicHttpPayloadDecoder::ResumeDecodingPayload"
           << "  remaining_payload=" << state->remaining_payload()
           << "  db->Remaining=" << db->Remaining();

  const QuicHttpFrameHeader& frame_header = state->frame_header();
  DCHECK_EQ(QuicHttpFrameType::PUSH_PROMISE, frame_header.type);
  DCHECK_LE(state->remaining_payload(), frame_header.payload_length);
  DCHECK_LE(db->Remaining(), frame_header.payload_length);

  QuicHttpDecodeStatus status;
  while (true) {
    DVLOG(2) << "QuicHttpPushPromiseQuicHttpPayloadDecoder::"
                "ResumeDecodingPayload payload_state_="
             << payload_state_;
    switch (payload_state_) {
      case PayloadState::kReadPadLength:
        DCHECK_EQ(state->remaining_payload(), frame_header.payload_length);
        // ReadPadLength handles the OnPadLength callback, and updating the
        // remaining_payload and remaining_padding fields. If the amount of
        // padding is too large to fit in the frame's payload, ReadPadLength
        // instead calls OnPaddingTooLong and returns kDecodeError.
        // Suppress the call to OnPadLength because we haven't yet called
        // OnPushPromiseStart, which needs to wait until we've decoded the
        // Promised Stream ID.
        status = state->ReadPadLength(db, /*report_pad_length*/ false);
        if (status != QuicHttpDecodeStatus::kDecodeDone) {
          payload_state_ = PayloadState::kReadPadLength;
          return status;
        }
      // FALLTHROUGH_INTENDED;

      case PayloadState::kStartDecodingPushPromiseFields:
        status =
            state->StartDecodingStructureInPayload(&push_promise_fields_, db);
        if (status != QuicHttpDecodeStatus::kDecodeDone) {
          payload_state_ = PayloadState::kResumeDecodingPushPromiseFields;
          return status;
        }
        // Finished decoding the Promised Stream ID. Can now tell the listener
        // that we're starting to decode a PUSH_PROMISE frame.
        ReportPushPromise(state);
      // FALLTHROUGH_INTENDED;

      case PayloadState::kReadPayload:
        DCHECK_LT(state->remaining_payload(), frame_header.payload_length);
        DCHECK_LE(state->remaining_payload(),
                  frame_header.payload_length -
                      QuicHttpPushPromiseFields::EncodedSize());
        DCHECK_LE(
            state->remaining_payload(),
            frame_header.payload_length -
                QuicHttpPushPromiseFields::EncodedSize() -
                (frame_header.IsPadded() ? (1 + state->remaining_padding())
                                         : 0));
        {
          size_t avail = state->AvailablePayload(db);
          state->listener()->OnHpackFragment(db->cursor(), avail);
          db->AdvanceCursor(avail);
          state->ConsumePayload(avail);
        }
        if (state->remaining_payload() > 0) {
          payload_state_ = PayloadState::kReadPayload;
          return QuicHttpDecodeStatus::kDecodeInProgress;
        }
      // FALLTHROUGH_INTENDED;

      case PayloadState::kSkipPadding:
        // SkipPadding handles the OnPadding callback.
        if (state->SkipPadding(db)) {
          state->listener()->OnPushPromiseEnd();
          return QuicHttpDecodeStatus::kDecodeDone;
        }
        payload_state_ = PayloadState::kSkipPadding;
        return QuicHttpDecodeStatus::kDecodeInProgress;

      case PayloadState::kResumeDecodingPushPromiseFields:
        status =
            state->ResumeDecodingStructureInPayload(&push_promise_fields_, db);
        if (status == QuicHttpDecodeStatus::kDecodeDone) {
          // Finished decoding the Promised Stream ID. Can now tell the listener
          // that we're starting to decode a PUSH_PROMISE frame.
          ReportPushPromise(state);
          payload_state_ = PayloadState::kReadPayload;
          continue;
        }
        payload_state_ = PayloadState::kResumeDecodingPushPromiseFields;
        return status;
    }
    QUIC_BUG << "PayloadState: " << payload_state_;
  }
}

void QuicHttpPushPromiseQuicHttpPayloadDecoder::ReportPushPromise(
    QuicHttpFrameDecoderState* state) {
  const QuicHttpFrameHeader& frame_header = state->frame_header();
  if (frame_header.IsPadded()) {
    state->listener()->OnPushPromiseStart(frame_header, push_promise_fields_,
                                          1 + state->remaining_padding());
  } else {
    state->listener()->OnPushPromiseStart(frame_header, push_promise_fields_,
                                          0);
  }
}

}  // namespace net
