// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "quiche/http2/decoder/payload_decoders/push_promise_payload_decoder.h"

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
                         PushPromisePayloadDecoder::PayloadState v) {
  switch (v) {
    case PushPromisePayloadDecoder::PayloadState::kReadPadLength:
      return out << "kReadPadLength";
    case PushPromisePayloadDecoder::PayloadState::
        kStartDecodingPushPromiseFields:
      return out << "kStartDecodingPushPromiseFields";
    case PushPromisePayloadDecoder::PayloadState::kReadPayload:
      return out << "kReadPayload";
    case PushPromisePayloadDecoder::PayloadState::kSkipPadding:
      return out << "kSkipPadding";
    case PushPromisePayloadDecoder::PayloadState::
        kResumeDecodingPushPromiseFields:
      return out << "kResumeDecodingPushPromiseFields";
  }
  return out << static_cast<int>(v);
}

DecodeStatus PushPromisePayloadDecoder::StartDecodingPayload(
    FrameDecoderState* state, DecodeBuffer* db) {
  const Http2FrameHeader& frame_header = state->frame_header();
  const uint32_t total_length = frame_header.payload_length;

  QUICHE_DVLOG(2) << "PushPromisePayloadDecoder::StartDecodingPayload: "
                  << frame_header;

  QUICHE_DCHECK_EQ(Http2FrameType::PUSH_PROMISE, frame_header.type);
  QUICHE_DCHECK_LE(db->Remaining(), total_length);
  QUICHE_DCHECK_EQ(0, frame_header.flags & ~(Http2FrameFlag::END_HEADERS |
                                             Http2FrameFlag::PADDED));

  if (!frame_header.IsPadded()) {
    // If it turns out that PUSH_PROMISE frames without padding are sufficiently
    // common, and that they are usually short enough that they fit entirely
    // into one DecodeBuffer, we can detect that here and implement a special
    // case, avoiding the state machine in ResumeDecodingPayload.
    payload_state_ = PayloadState::kStartDecodingPushPromiseFields;
  } else {
    payload_state_ = PayloadState::kReadPadLength;
  }
  state->InitializeRemainders();
  return ResumeDecodingPayload(state, db);
}

DecodeStatus PushPromisePayloadDecoder::ResumeDecodingPayload(
    FrameDecoderState* state, DecodeBuffer* db) {
  QUICHE_DVLOG(2) << "UnknownPayloadDecoder::ResumeDecodingPayload"
                  << "  remaining_payload=" << state->remaining_payload()
                  << "  db->Remaining=" << db->Remaining();

  const Http2FrameHeader& frame_header = state->frame_header();
  QUICHE_DCHECK_EQ(Http2FrameType::PUSH_PROMISE, frame_header.type);
  QUICHE_DCHECK_LE(state->remaining_payload(), frame_header.payload_length);
  QUICHE_DCHECK_LE(db->Remaining(), frame_header.payload_length);

  DecodeStatus status;
  while (true) {
    QUICHE_DVLOG(2)
        << "PushPromisePayloadDecoder::ResumeDecodingPayload payload_state_="
        << payload_state_;
    switch (payload_state_) {
      case PayloadState::kReadPadLength:
        QUICHE_DCHECK_EQ(state->remaining_payload(),
                         frame_header.payload_length);
        // ReadPadLength handles the OnPadLength callback, and updating the
        // remaining_payload and remaining_padding fields. If the amount of
        // padding is too large to fit in the frame's payload, ReadPadLength
        // instead calls OnPaddingTooLong and returns kDecodeError.
        // Suppress the call to OnPadLength because we haven't yet called
        // OnPushPromiseStart, which needs to wait until we've decoded the
        // Promised Stream ID.
        status = state->ReadPadLength(db, /*report_pad_length*/ false);
        if (status != DecodeStatus::kDecodeDone) {
          payload_state_ = PayloadState::kReadPadLength;
          return status;
        }
        ABSL_FALLTHROUGH_INTENDED;

      case PayloadState::kStartDecodingPushPromiseFields:
        status =
            state->StartDecodingStructureInPayload(&push_promise_fields_, db);
        if (status != DecodeStatus::kDecodeDone) {
          payload_state_ = PayloadState::kResumeDecodingPushPromiseFields;
          return status;
        }
        // Finished decoding the Promised Stream ID. Can now tell the listener
        // that we're starting to decode a PUSH_PROMISE frame.
        ReportPushPromise(state);
        ABSL_FALLTHROUGH_INTENDED;

      case PayloadState::kReadPayload:
        QUICHE_DCHECK_LT(state->remaining_payload(),
                         frame_header.payload_length);
        QUICHE_DCHECK_LE(state->remaining_payload(),
                         frame_header.payload_length -
                             Http2PushPromiseFields::EncodedSize());
        QUICHE_DCHECK_LE(
            state->remaining_payload(),
            frame_header.payload_length -
                Http2PushPromiseFields::EncodedSize() -
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
          return DecodeStatus::kDecodeInProgress;
        }
        ABSL_FALLTHROUGH_INTENDED;

      case PayloadState::kSkipPadding:
        // SkipPadding handles the OnPadding callback.
        if (state->SkipPadding(db)) {
          state->listener()->OnPushPromiseEnd();
          return DecodeStatus::kDecodeDone;
        }
        payload_state_ = PayloadState::kSkipPadding;
        return DecodeStatus::kDecodeInProgress;

      case PayloadState::kResumeDecodingPushPromiseFields:
        status =
            state->ResumeDecodingStructureInPayload(&push_promise_fields_, db);
        if (status == DecodeStatus::kDecodeDone) {
          // Finished decoding the Promised Stream ID. Can now tell the listener
          // that we're starting to decode a PUSH_PROMISE frame.
          ReportPushPromise(state);
          payload_state_ = PayloadState::kReadPayload;
          continue;
        }
        payload_state_ = PayloadState::kResumeDecodingPushPromiseFields;
        return status;
    }
    QUICHE_BUG(http2_bug_183_1) << "PayloadState: " << payload_state_;
  }
}

void PushPromisePayloadDecoder::ReportPushPromise(FrameDecoderState* state) {
  const Http2FrameHeader& frame_header = state->frame_header();
  if (frame_header.IsPadded()) {
    state->listener()->OnPushPromiseStart(frame_header, push_promise_fields_,
                                          1 + state->remaining_padding());
  } else {
    state->listener()->OnPushPromiseStart(frame_header, push_promise_fields_,
                                          0);
  }
}

}  // namespace http2
