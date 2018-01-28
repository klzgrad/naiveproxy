// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/http2/decoder/payload_decoders/headers_payload_decoder.h"

#include <stddef.h>

#include "base/logging.h"
#include "base/macros.h"
#include "net/http2/decoder/decode_buffer.h"
#include "net/http2/decoder/http2_frame_decoder_listener.h"
#include "net/http2/http2_constants.h"
#include "net/http2/http2_structures.h"
#include "net/http2/tools/http2_bug_tracker.h"

namespace net {

std::ostream& operator<<(std::ostream& out,
                         HeadersPayloadDecoder::PayloadState v) {
  switch (v) {
    case HeadersPayloadDecoder::PayloadState::kReadPadLength:
      return out << "kReadPadLength";
    case HeadersPayloadDecoder::PayloadState::kStartDecodingPriorityFields:
      return out << "kStartDecodingPriorityFields";
    case HeadersPayloadDecoder::PayloadState::kResumeDecodingPriorityFields:
      return out << "kResumeDecodingPriorityFields";
    case HeadersPayloadDecoder::PayloadState::kReadPayload:
      return out << "kReadPayload";
    case HeadersPayloadDecoder::PayloadState::kSkipPadding:
      return out << "kSkipPadding";
  }
  // Since the value doesn't come over the wire, only a programming bug should
  // result in reaching this point.
  int unknown = static_cast<int>(v);
  HTTP2_BUG << "Invalid HeadersPayloadDecoder::PayloadState: " << unknown;
  return out << "HeadersPayloadDecoder::PayloadState(" << unknown << ")";
}

DecodeStatus HeadersPayloadDecoder::StartDecodingPayload(
    FrameDecoderState* state,
    DecodeBuffer* db) {
  const Http2FrameHeader& frame_header = state->frame_header();
  const uint32_t total_length = frame_header.payload_length;

  DVLOG(2) << "HeadersPayloadDecoder::StartDecodingPayload: " << frame_header;

  DCHECK_EQ(Http2FrameType::HEADERS, frame_header.type);
  DCHECK_LE(db->Remaining(), total_length);
  DCHECK_EQ(0, frame_header.flags &
                   ~(Http2FrameFlag::END_STREAM | Http2FrameFlag::END_HEADERS |
                     Http2FrameFlag::PADDED | Http2FrameFlag::PRIORITY));

  // Special case for HEADERS frames that contain only the HPACK block
  // (fragment or whole) and that fit fully into the decode buffer.
  // Why? Unencoded browser GET requests are typically under 1K and HPACK
  // commonly shrinks request headers by 80%, so we can expect this to
  // be common.
  // TODO(jamessynge) Add counters here and to Spdy for determining how
  // common this situation is. A possible approach is to create a
  // Http2FrameDecoderListener that counts the callbacks and then forwards
  // them on to another listener, which makes it easy to add and remove
  // counting on a connection or even frame basis.

  // PADDED and PRIORITY both extra steps to decode, but if neither flag is
  // set then we can decode faster.
  const auto payload_flags = Http2FrameFlag::PADDED | Http2FrameFlag::PRIORITY;
  if (!frame_header.HasAnyFlags(payload_flags)) {
    DVLOG(2) << "StartDecodingPayload !IsPadded && !HasPriority";
    if (db->Remaining() == total_length) {
      DVLOG(2) << "StartDecodingPayload all present";
      // Note that we don't cache the listener field so that the callee can
      // replace it if the frame is bad.
      // If this case is common enough, consider combining the 3 callbacks
      // into one, especially if END_HEADERS is also set.
      state->listener()->OnHeadersStart(frame_header);
      if (total_length > 0) {
        state->listener()->OnHpackFragment(db->cursor(), total_length);
        db->AdvanceCursor(total_length);
      }
      state->listener()->OnHeadersEnd();
      return DecodeStatus::kDecodeDone;
    }
    payload_state_ = PayloadState::kReadPayload;
  } else if (frame_header.IsPadded()) {
    payload_state_ = PayloadState::kReadPadLength;
  } else {
    DCHECK(frame_header.HasPriority()) << frame_header;
    payload_state_ = PayloadState::kStartDecodingPriorityFields;
  }
  state->InitializeRemainders();
  state->listener()->OnHeadersStart(frame_header);
  return ResumeDecodingPayload(state, db);
}

DecodeStatus HeadersPayloadDecoder::ResumeDecodingPayload(
    FrameDecoderState* state,
    DecodeBuffer* db) {
  DVLOG(2) << "HeadersPayloadDecoder::ResumeDecodingPayload "
           << "remaining_payload=" << state->remaining_payload()
           << "; db->Remaining=" << db->Remaining();

  const Http2FrameHeader& frame_header = state->frame_header();

  DCHECK_EQ(Http2FrameType::HEADERS, frame_header.type);
  DCHECK_LE(state->remaining_payload_and_padding(),
            frame_header.payload_length);
  DCHECK_LE(db->Remaining(), state->remaining_payload_and_padding());
  DecodeStatus status;
  size_t avail;
  while (true) {
    DVLOG(2) << "HeadersPayloadDecoder::ResumeDecodingPayload payload_state_="
             << payload_state_;
    switch (payload_state_) {
      case PayloadState::kReadPadLength:
        // ReadPadLength handles the OnPadLength callback, and updating the
        // remaining_payload and remaining_padding fields. If the amount of
        // padding is too large to fit in the frame's payload, ReadPadLength
        // instead calls OnPaddingTooLong and returns kDecodeError.
        status = state->ReadPadLength(db, /*report_pad_length*/ true);
        if (status != DecodeStatus::kDecodeDone) {
          return status;
        }
        if (!frame_header.HasPriority()) {
          payload_state_ = PayloadState::kReadPayload;
          continue;
        }
      // FALLTHROUGH_INTENDED

      case PayloadState::kStartDecodingPriorityFields:
        status = state->StartDecodingStructureInPayload(&priority_fields_, db);
        if (status != DecodeStatus::kDecodeDone) {
          payload_state_ = PayloadState::kResumeDecodingPriorityFields;
          return status;
        }
        state->listener()->OnHeadersPriority(priority_fields_);
      // FALLTHROUGH_INTENDED

      case PayloadState::kReadPayload:
        avail = state->AvailablePayload(db);
        if (avail > 0) {
          state->listener()->OnHpackFragment(db->cursor(), avail);
          db->AdvanceCursor(avail);
          state->ConsumePayload(avail);
        }
        if (state->remaining_payload() > 0) {
          payload_state_ = PayloadState::kReadPayload;
          return DecodeStatus::kDecodeInProgress;
        }
      // FALLTHROUGH_INTENDED

      case PayloadState::kSkipPadding:
        // SkipPadding handles the OnPadding callback.
        if (state->SkipPadding(db)) {
          state->listener()->OnHeadersEnd();
          return DecodeStatus::kDecodeDone;
        }
        payload_state_ = PayloadState::kSkipPadding;
        return DecodeStatus::kDecodeInProgress;

      case PayloadState::kResumeDecodingPriorityFields:
        status = state->ResumeDecodingStructureInPayload(&priority_fields_, db);
        if (status != DecodeStatus::kDecodeDone) {
          return status;
        }
        state->listener()->OnHeadersPriority(priority_fields_);
        payload_state_ = PayloadState::kReadPayload;
        continue;
    }
    HTTP2_BUG << "PayloadState: " << payload_state_;
  }
}

}  // namespace net
