// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/quic/http/decoder/payload_decoders/quic_http_headers_payload_decoder.h"

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
    QuicHttpHeadersQuicHttpPayloadDecoder::PayloadState v) {
  switch (v) {
    case QuicHttpHeadersQuicHttpPayloadDecoder::PayloadState::kReadPadLength:
      return out << "kReadPadLength";
    case QuicHttpHeadersQuicHttpPayloadDecoder::PayloadState::
        kStartDecodingPriorityFields:
      return out << "kStartDecodingPriorityFields";
    case QuicHttpHeadersQuicHttpPayloadDecoder::PayloadState::
        kResumeDecodingPriorityFields:
      return out << "kResumeDecodingPriorityFields";
    case QuicHttpHeadersQuicHttpPayloadDecoder::PayloadState::kReadPayload:
      return out << "kReadPayload";
    case QuicHttpHeadersQuicHttpPayloadDecoder::PayloadState::kSkipPadding:
      return out << "kSkipPadding";
  }
  // Since the value doesn't come over the wire, only a programming bug should
  // result in reaching this point.
  int unknown = static_cast<int>(v);
  QUIC_BUG << "Invalid QuicHttpHeadersQuicHttpPayloadDecoder::PayloadState: "
           << unknown;
  return out << "QuicHttpHeadersQuicHttpPayloadDecoder::PayloadState("
             << unknown << ")";
}

QuicHttpDecodeStatus
QuicHttpHeadersQuicHttpPayloadDecoder::StartDecodingPayload(
    QuicHttpFrameDecoderState* state,
    QuicHttpDecodeBuffer* db) {
  const QuicHttpFrameHeader& frame_header = state->frame_header();
  const uint32_t total_length = frame_header.payload_length;

  DVLOG(2) << "QuicHttpHeadersQuicHttpPayloadDecoder::StartDecodingPayload: "
           << frame_header;

  DCHECK_EQ(QuicHttpFrameType::HEADERS, frame_header.type);
  DCHECK_LE(db->Remaining(), total_length);
  DCHECK_EQ(0, frame_header.flags & ~(QuicHttpFrameFlag::QUIC_HTTP_END_STREAM |
                                      QuicHttpFrameFlag::QUIC_HTTP_END_HEADERS |
                                      QuicHttpFrameFlag::QUIC_HTTP_PADDED |
                                      QuicHttpFrameFlag::QUIC_HTTP_PRIORITY));

  // Special case for HEADERS frames that contain only the HPQUIC_HTTP_ACK block
  // (fragment or whole) and that fit fully into the decode buffer.
  // Why? Unencoded browser GET requests are typically under 1K and
  // HPQUIC_HTTP_ACK commonly shrinks request headers by 80%, so we can expect
  // this to be common.
  // TODO(jamessynge) Add counters here and to Spdy for determining how
  // common this situation is. A possible approach is to create a
  // QuicHttpFrameDecoderListener that counts the callbacks and then forwards
  // them on to another listener, which makes it easy to add and remove
  // counting on a connection or even frame basis.

  // QUIC_HTTP_PADDED and QUIC_HTTP_PRIORITY both extra steps to decode, but if
  // neither flag is set then we can decode faster.
  const auto payload_flags = QuicHttpFrameFlag::QUIC_HTTP_PADDED |
                             QuicHttpFrameFlag::QUIC_HTTP_PRIORITY;
  if (!frame_header.HasAnyFlags(payload_flags)) {
    DVLOG(2) << "StartDecodingPayload !IsPadded && !HasPriority";
    if (db->Remaining() == total_length) {
      DVLOG(2) << "StartDecodingPayload all present";
      // Note that we don't cache the listener field so that the callee can
      // replace it if the frame is bad.
      // If this case is common enough, consider combining the 3 callbacks
      // into one, especially if QUIC_HTTP_END_HEADERS is also set.
      state->listener()->OnHeadersStart(frame_header);
      if (total_length > 0) {
        state->listener()->OnHpackFragment(db->cursor(), total_length);
        db->AdvanceCursor(total_length);
      }
      state->listener()->OnHeadersEnd();
      return QuicHttpDecodeStatus::kDecodeDone;
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

QuicHttpDecodeStatus
QuicHttpHeadersQuicHttpPayloadDecoder::ResumeDecodingPayload(
    QuicHttpFrameDecoderState* state,
    QuicHttpDecodeBuffer* db) {
  DVLOG(2) << "QuicHttpHeadersQuicHttpPayloadDecoder::ResumeDecodingPayload "
           << "remaining_payload=" << state->remaining_payload()
           << "; db->Remaining=" << db->Remaining();

  const QuicHttpFrameHeader& frame_header = state->frame_header();

  DCHECK_EQ(QuicHttpFrameType::HEADERS, frame_header.type);
  DCHECK_LE(state->remaining_payload_and_padding(),
            frame_header.payload_length);
  DCHECK_LE(db->Remaining(), state->remaining_payload_and_padding());
  QuicHttpDecodeStatus status;
  size_t avail;
  while (true) {
    DVLOG(2) << "QuicHttpHeadersQuicHttpPayloadDecoder::ResumeDecodingPayload "
                "payload_state_="
             << payload_state_;
    switch (payload_state_) {
      case PayloadState::kReadPadLength:
        // ReadPadLength handles the OnPadLength callback, and updating the
        // remaining_payload and remaining_padding fields. If the amount of
        // padding is too large to fit in the frame's payload, ReadPadLength
        // instead calls OnPaddingTooLong and returns kDecodeError.
        status = state->ReadPadLength(db, /*report_pad_length*/ true);
        if (status != QuicHttpDecodeStatus::kDecodeDone) {
          return status;
        }
        if (!frame_header.HasPriority()) {
          payload_state_ = PayloadState::kReadPayload;
          continue;
        }
      // FALLTHROUGH_INTENDED;

      case PayloadState::kStartDecodingPriorityFields:
        status = state->StartDecodingStructureInPayload(&priority_fields_, db);
        if (status != QuicHttpDecodeStatus::kDecodeDone) {
          payload_state_ = PayloadState::kResumeDecodingPriorityFields;
          return status;
        }
        state->listener()->OnHeadersPriority(priority_fields_);
      // FALLTHROUGH_INTENDED;

      case PayloadState::kReadPayload:
        avail = state->AvailablePayload(db);
        if (avail > 0) {
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
          state->listener()->OnHeadersEnd();
          return QuicHttpDecodeStatus::kDecodeDone;
        }
        payload_state_ = PayloadState::kSkipPadding;
        return QuicHttpDecodeStatus::kDecodeInProgress;

      case PayloadState::kResumeDecodingPriorityFields:
        status = state->ResumeDecodingStructureInPayload(&priority_fields_, db);
        if (status != QuicHttpDecodeStatus::kDecodeDone) {
          return status;
        }
        state->listener()->OnHeadersPriority(priority_fields_);
        payload_state_ = PayloadState::kReadPayload;
        continue;
    }
    QUIC_BUG << "PayloadState: " << payload_state_;
  }
}

}  // namespace net
