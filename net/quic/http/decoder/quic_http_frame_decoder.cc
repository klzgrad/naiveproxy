// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/quic/http/decoder/quic_http_frame_decoder.h"

#include "net/quic/http/quic_http_constants.h"
#include "net/quic/platform/api/quic_bug_tracker.h"
#include "net/quic/platform/api/quic_logging.h"

namespace net {

std::ostream& operator<<(std::ostream& out, QuicHttpFrameDecoder::State v) {
  switch (v) {
    case QuicHttpFrameDecoder::State::kStartDecodingHeader:
      return out << "kStartDecodingHeader";
    case QuicHttpFrameDecoder::State::kResumeDecodingHeader:
      return out << "kResumeDecodingHeader";
    case QuicHttpFrameDecoder::State::kResumeDecodingPayload:
      return out << "kResumeDecodingPayload";
    case QuicHttpFrameDecoder::State::kDiscardPayload:
      return out << "kDiscardPayload";
  }
  // Since the value doesn't come over the wire, only a programming bug should
  // result in reaching this point.
  int unknown = static_cast<int>(v);
  QUIC_BUG << "QuicHttpFrameDecoder::State " << unknown;
  return out << "QuicHttpFrameDecoder::State(" << unknown << ")";
}

QuicHttpFrameDecoder::QuicHttpFrameDecoder(
    QuicHttpFrameDecoderListener* listener)
    : state_(State::kStartDecodingHeader),
      maximum_payload_size_(QuicHttpSettingsInfo::DefaultMaxFrameSize()) {
  set_listener(listener);
}

void QuicHttpFrameDecoder::set_listener(
    QuicHttpFrameDecoderListener* listener) {
  if (listener == nullptr) {
    listener = &no_op_listener_;
  }
  frame_decoder_state_.set_listener(listener);
}

QuicHttpFrameDecoderListener* QuicHttpFrameDecoder::listener() const {
  return frame_decoder_state_.listener();
}

QuicHttpDecodeStatus QuicHttpFrameDecoder::DecodeFrame(
    QuicHttpDecodeBuffer* db) {
  DVLOG(2) << "QuicHttpFrameDecoder::DecodeFrame state=" << state_;
  switch (state_) {
    case State::kStartDecodingHeader:
      if (frame_decoder_state_.StartDecodingFrameHeader(db)) {
        return StartDecodingPayload(db);
      }
      state_ = State::kResumeDecodingHeader;
      return QuicHttpDecodeStatus::kDecodeInProgress;

    case State::kResumeDecodingHeader:
      if (frame_decoder_state_.ResumeDecodingFrameHeader(db)) {
        return StartDecodingPayload(db);
      }
      return QuicHttpDecodeStatus::kDecodeInProgress;

    case State::kResumeDecodingPayload:
      return ResumeDecodingPayload(db);

    case State::kDiscardPayload:
      return DiscardPayload(db);
  }

  QUIC_NOTREACHED();
  return QuicHttpDecodeStatus::kDecodeError;
}

size_t QuicHttpFrameDecoder::remaining_payload() const {
  return frame_decoder_state_.remaining_payload();
}

uint32_t QuicHttpFrameDecoder::remaining_padding() const {
  return frame_decoder_state_.remaining_padding();
}

QuicHttpDecodeStatus QuicHttpFrameDecoder::StartDecodingPayload(
    QuicHttpDecodeBuffer* db) {
  const QuicHttpFrameHeader& header = frame_header();

  // TODO(jamessynge): Remove OnFrameHeader once done with supporting
  // SpdyFramer's exact states.
  if (!listener()->OnFrameHeader(header)) {
    DVLOG(2) << "OnFrameHeader rejected the frame, will discard; header: "
             << header;
    state_ = State::kDiscardPayload;
    frame_decoder_state_.InitializeRemainders();
    return QuicHttpDecodeStatus::kDecodeError;
  }

  if (header.payload_length > maximum_payload_size_) {
    DVLOG(2) << "Payload length is greater than allowed: "
             << header.payload_length << " > " << maximum_payload_size_
             << "\n   header: " << header;
    state_ = State::kDiscardPayload;
    frame_decoder_state_.InitializeRemainders();
    listener()->OnFrameSizeError(header);
    return QuicHttpDecodeStatus::kDecodeError;
  }

  // The decode buffer can extend across many frames. Make sure that the
  // buffer we pass to the start method that is specific to the frame type
  // does not exend beyond this frame.
  QuicHttpDecodeBufferSubset subset(db, header.payload_length);
  QuicHttpDecodeStatus status;
  switch (header.type) {
    case QuicHttpFrameType::DATA:
      status = StartDecodingDataPayload(&subset);
      break;

    case QuicHttpFrameType::HEADERS:
      status = StartDecodingHeadersPayload(&subset);
      break;

    case QuicHttpFrameType::QUIC_HTTP_PRIORITY:
      status = StartDecodingPriorityPayload(&subset);
      break;

    case QuicHttpFrameType::RST_STREAM:
      status = StartDecodingRstStreamPayload(&subset);
      break;

    case QuicHttpFrameType::SETTINGS:
      status = StartDecodingSettingsPayload(&subset);
      break;

    case QuicHttpFrameType::PUSH_PROMISE:
      status = StartDecodingPushPromisePayload(&subset);
      break;

    case QuicHttpFrameType::PING:
      status = StartDecodingPingPayload(&subset);
      break;

    case QuicHttpFrameType::GOAWAY:
      status = StartDecodingGoAwayPayload(&subset);
      break;

    case QuicHttpFrameType::WINDOW_UPDATE:
      status = StartDecodingWindowUpdatePayload(&subset);
      break;

    case QuicHttpFrameType::CONTINUATION:
      status = StartDecodingContinuationPayload(&subset);
      break;

    case QuicHttpFrameType::ALTSVC:
      status = StartDecodingAltSvcPayload(&subset);
      break;

    default:
      status = StartDecodingUnknownPayload(&subset);
      break;
  }

  if (status == QuicHttpDecodeStatus::kDecodeDone) {
    state_ = State::kStartDecodingHeader;
    return status;
  } else if (status == QuicHttpDecodeStatus::kDecodeInProgress) {
    state_ = State::kResumeDecodingPayload;
    return status;
  } else {
    state_ = State::kDiscardPayload;
    return status;
  }
}

QuicHttpDecodeStatus QuicHttpFrameDecoder::ResumeDecodingPayload(
    QuicHttpDecodeBuffer* db) {
  // The decode buffer can extend across many frames. Make sure that the
  // buffer we pass to the start method that is specific to the frame type
  // does not exend beyond this frame.
  size_t remaining = frame_decoder_state_.remaining_total_payload();
  DCHECK_LE(remaining, frame_header().payload_length);
  QuicHttpDecodeBufferSubset subset(db, remaining);
  QuicHttpDecodeStatus status;
  switch (frame_header().type) {
    case QuicHttpFrameType::DATA:
      status = ResumeDecodingDataPayload(&subset);
      break;

    case QuicHttpFrameType::HEADERS:
      status = ResumeDecodingHeadersPayload(&subset);
      break;

    case QuicHttpFrameType::QUIC_HTTP_PRIORITY:
      status = ResumeDecodingPriorityPayload(&subset);
      break;

    case QuicHttpFrameType::RST_STREAM:
      status = ResumeDecodingRstStreamPayload(&subset);
      break;

    case QuicHttpFrameType::SETTINGS:
      status = ResumeDecodingSettingsPayload(&subset);
      break;

    case QuicHttpFrameType::PUSH_PROMISE:
      status = ResumeDecodingPushPromisePayload(&subset);
      break;

    case QuicHttpFrameType::PING:
      status = ResumeDecodingPingPayload(&subset);
      break;

    case QuicHttpFrameType::GOAWAY:
      status = ResumeDecodingGoAwayPayload(&subset);
      break;

    case QuicHttpFrameType::WINDOW_UPDATE:
      status = ResumeDecodingWindowUpdatePayload(&subset);
      break;

    case QuicHttpFrameType::CONTINUATION:
      status = ResumeDecodingContinuationPayload(&subset);
      break;

    case QuicHttpFrameType::ALTSVC:
      status = ResumeDecodingAltSvcPayload(&subset);
      break;

    default:
      status = ResumeDecodingUnknownPayload(&subset);
      break;
  }

  if (status == QuicHttpDecodeStatus::kDecodeDone) {
    state_ = State::kStartDecodingHeader;
    return status;
  } else if (status == QuicHttpDecodeStatus::kDecodeInProgress) {
    return status;
  } else {
    state_ = State::kDiscardPayload;
    return status;
  }
}

// Clear any of the flags in the frame header that aren't set in valid_flags.
void QuicHttpFrameDecoder::RetainFlags(uint8_t valid_flags) {
  frame_decoder_state_.RetainFlags(valid_flags);
}

// Clear all of the flags in the frame header; for use with frame types that
// don't define any flags, such as WINDOW_UPDATE.
void QuicHttpFrameDecoder::ClearFlags() {
  frame_decoder_state_.ClearFlags();
}

QuicHttpDecodeStatus QuicHttpFrameDecoder::StartDecodingAltSvcPayload(
    QuicHttpDecodeBuffer* db) {
  ClearFlags();
  return altsvc_payload_decoder_.StartDecodingPayload(&frame_decoder_state_,
                                                      db);
}
QuicHttpDecodeStatus QuicHttpFrameDecoder::ResumeDecodingAltSvcPayload(
    QuicHttpDecodeBuffer* db) {
  // The frame is not paddable.
  DCHECK_EQ(frame_decoder_state_.remaining_total_payload(),
            frame_decoder_state_.remaining_payload());
  return altsvc_payload_decoder_.ResumeDecodingPayload(&frame_decoder_state_,
                                                       db);
}

QuicHttpDecodeStatus QuicHttpFrameDecoder::StartDecodingContinuationPayload(
    QuicHttpDecodeBuffer* db) {
  RetainFlags(QuicHttpFrameFlag::QUIC_HTTP_END_HEADERS);
  return continuation_payload_decoder_.StartDecodingPayload(
      &frame_decoder_state_, db);
}
QuicHttpDecodeStatus QuicHttpFrameDecoder::ResumeDecodingContinuationPayload(
    QuicHttpDecodeBuffer* db) {
  // The frame is not paddable.
  DCHECK_EQ(frame_decoder_state_.remaining_total_payload(),
            frame_decoder_state_.remaining_payload());
  return continuation_payload_decoder_.ResumeDecodingPayload(
      &frame_decoder_state_, db);
}

QuicHttpDecodeStatus QuicHttpFrameDecoder::StartDecodingDataPayload(
    QuicHttpDecodeBuffer* db) {
  RetainFlags(QuicHttpFrameFlag::QUIC_HTTP_END_STREAM |
              QuicHttpFrameFlag::QUIC_HTTP_PADDED);
  return data_payload_decoder_.StartDecodingPayload(&frame_decoder_state_, db);
}
QuicHttpDecodeStatus QuicHttpFrameDecoder::ResumeDecodingDataPayload(
    QuicHttpDecodeBuffer* db) {
  return data_payload_decoder_.ResumeDecodingPayload(&frame_decoder_state_, db);
}

QuicHttpDecodeStatus QuicHttpFrameDecoder::StartDecodingGoAwayPayload(
    QuicHttpDecodeBuffer* db) {
  ClearFlags();
  return goaway_payload_decoder_.StartDecodingPayload(&frame_decoder_state_,
                                                      db);
}
QuicHttpDecodeStatus QuicHttpFrameDecoder::ResumeDecodingGoAwayPayload(
    QuicHttpDecodeBuffer* db) {
  // The frame is not paddable.
  DCHECK_EQ(frame_decoder_state_.remaining_total_payload(),
            frame_decoder_state_.remaining_payload());
  return goaway_payload_decoder_.ResumeDecodingPayload(&frame_decoder_state_,
                                                       db);
}

QuicHttpDecodeStatus QuicHttpFrameDecoder::StartDecodingHeadersPayload(
    QuicHttpDecodeBuffer* db) {
  RetainFlags(QuicHttpFrameFlag::QUIC_HTTP_END_STREAM |
              QuicHttpFrameFlag::QUIC_HTTP_END_HEADERS |
              QuicHttpFrameFlag::QUIC_HTTP_PADDED |
              QuicHttpFrameFlag::QUIC_HTTP_PRIORITY);
  return headers_payload_decoder_.StartDecodingPayload(&frame_decoder_state_,
                                                       db);
}
QuicHttpDecodeStatus QuicHttpFrameDecoder::ResumeDecodingHeadersPayload(
    QuicHttpDecodeBuffer* db) {
  DCHECK_LE(frame_decoder_state_.remaining_payload_and_padding(),
            frame_header().payload_length);
  return headers_payload_decoder_.ResumeDecodingPayload(&frame_decoder_state_,
                                                        db);
}

QuicHttpDecodeStatus QuicHttpFrameDecoder::StartDecodingPingPayload(
    QuicHttpDecodeBuffer* db) {
  RetainFlags(QuicHttpFrameFlag::QUIC_HTTP_ACK);
  return ping_payload_decoder_.StartDecodingPayload(&frame_decoder_state_, db);
}
QuicHttpDecodeStatus QuicHttpFrameDecoder::ResumeDecodingPingPayload(
    QuicHttpDecodeBuffer* db) {
  // The frame is not paddable.
  DCHECK_EQ(frame_decoder_state_.remaining_total_payload(),
            frame_decoder_state_.remaining_payload());
  return ping_payload_decoder_.ResumeDecodingPayload(&frame_decoder_state_, db);
}

QuicHttpDecodeStatus QuicHttpFrameDecoder::StartDecodingPriorityPayload(
    QuicHttpDecodeBuffer* db) {
  ClearFlags();
  return priority_payload_decoder_.StartDecodingPayload(&frame_decoder_state_,
                                                        db);
}
QuicHttpDecodeStatus QuicHttpFrameDecoder::ResumeDecodingPriorityPayload(
    QuicHttpDecodeBuffer* db) {
  // The frame is not paddable.
  DCHECK_EQ(frame_decoder_state_.remaining_total_payload(),
            frame_decoder_state_.remaining_payload());
  return priority_payload_decoder_.ResumeDecodingPayload(&frame_decoder_state_,
                                                         db);
}

QuicHttpDecodeStatus QuicHttpFrameDecoder::StartDecodingPushPromisePayload(
    QuicHttpDecodeBuffer* db) {
  RetainFlags(QuicHttpFrameFlag::QUIC_HTTP_END_HEADERS |
              QuicHttpFrameFlag::QUIC_HTTP_PADDED);
  return push_promise_payload_decoder_.StartDecodingPayload(
      &frame_decoder_state_, db);
}
QuicHttpDecodeStatus QuicHttpFrameDecoder::ResumeDecodingPushPromisePayload(
    QuicHttpDecodeBuffer* db) {
  DCHECK_LE(frame_decoder_state_.remaining_payload_and_padding(),
            frame_header().payload_length);
  return push_promise_payload_decoder_.ResumeDecodingPayload(
      &frame_decoder_state_, db);
}

QuicHttpDecodeStatus QuicHttpFrameDecoder::StartDecodingRstStreamPayload(
    QuicHttpDecodeBuffer* db) {
  ClearFlags();
  return rst_stream_payload_decoder_.StartDecodingPayload(&frame_decoder_state_,
                                                          db);
}
QuicHttpDecodeStatus QuicHttpFrameDecoder::ResumeDecodingRstStreamPayload(
    QuicHttpDecodeBuffer* db) {
  // The frame is not paddable.
  DCHECK_EQ(frame_decoder_state_.remaining_total_payload(),
            frame_decoder_state_.remaining_payload());
  return rst_stream_payload_decoder_.ResumeDecodingPayload(
      &frame_decoder_state_, db);
}

QuicHttpDecodeStatus QuicHttpFrameDecoder::StartDecodingSettingsPayload(
    QuicHttpDecodeBuffer* db) {
  RetainFlags(QuicHttpFrameFlag::QUIC_HTTP_ACK);
  return settings_payload_decoder_.StartDecodingPayload(&frame_decoder_state_,
                                                        db);
}
QuicHttpDecodeStatus QuicHttpFrameDecoder::ResumeDecodingSettingsPayload(
    QuicHttpDecodeBuffer* db) {
  // The frame is not paddable.
  DCHECK_EQ(frame_decoder_state_.remaining_total_payload(),
            frame_decoder_state_.remaining_payload());
  return settings_payload_decoder_.ResumeDecodingPayload(&frame_decoder_state_,
                                                         db);
}

QuicHttpDecodeStatus QuicHttpFrameDecoder::StartDecodingUnknownPayload(
    QuicHttpDecodeBuffer* db) {
  // We don't known what type of frame this is, so we don't know which flags
  // are valid, so we don't touch them.
  return unknown_payload_decoder_.StartDecodingPayload(&frame_decoder_state_,
                                                       db);
}
QuicHttpDecodeStatus QuicHttpFrameDecoder::ResumeDecodingUnknownPayload(
    QuicHttpDecodeBuffer* db) {
  // We don't known what type of frame this is, so we treat it as not paddable.
  DCHECK_EQ(frame_decoder_state_.remaining_total_payload(),
            frame_decoder_state_.remaining_payload());
  return unknown_payload_decoder_.ResumeDecodingPayload(&frame_decoder_state_,
                                                        db);
}

QuicHttpDecodeStatus QuicHttpFrameDecoder::StartDecodingWindowUpdatePayload(
    QuicHttpDecodeBuffer* db) {
  ClearFlags();
  return window_update_payload_decoder_.StartDecodingPayload(
      &frame_decoder_state_, db);
}
QuicHttpDecodeStatus QuicHttpFrameDecoder::ResumeDecodingWindowUpdatePayload(
    QuicHttpDecodeBuffer* db) {
  // The frame is not paddable.
  DCHECK_EQ(frame_decoder_state_.remaining_total_payload(),
            frame_decoder_state_.remaining_payload());
  return window_update_payload_decoder_.ResumeDecodingPayload(
      &frame_decoder_state_, db);
}

QuicHttpDecodeStatus QuicHttpFrameDecoder::DiscardPayload(
    QuicHttpDecodeBuffer* db) {
  DVLOG(2) << "remaining_payload=" << frame_decoder_state_.remaining_payload_
           << "; remaining_padding=" << frame_decoder_state_.remaining_padding_;
  frame_decoder_state_.remaining_payload_ +=
      frame_decoder_state_.remaining_padding_;
  frame_decoder_state_.remaining_padding_ = 0;
  const size_t avail = frame_decoder_state_.AvailablePayload(db);
  DVLOG(2) << "avail=" << avail;
  if (avail > 0) {
    frame_decoder_state_.ConsumePayload(avail);
    db->AdvanceCursor(avail);
  }
  if (frame_decoder_state_.remaining_payload_ == 0) {
    state_ = State::kStartDecodingHeader;
    return QuicHttpDecodeStatus::kDecodeDone;
  }
  return QuicHttpDecodeStatus::kDecodeInProgress;
}

}  // namespace net
