// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/third_party/quiche/src/http2/decoder/http2_frame_decoder.h"

#include "net/third_party/quiche/src/http2/decoder/decode_status.h"
#include "net/third_party/quiche/src/http2/hpack/varint/hpack_varint_decoder.h"
#include "net/third_party/quiche/src/http2/http2_constants.h"
#include "net/third_party/quiche/src/http2/platform/api/http2_bug_tracker.h"
#include "net/third_party/quiche/src/http2/platform/api/http2_macros.h"

namespace http2 {

std::ostream& operator<<(std::ostream& out, Http2FrameDecoder::State v) {
  switch (v) {
    case Http2FrameDecoder::State::kStartDecodingHeader:
      return out << "kStartDecodingHeader";
    case Http2FrameDecoder::State::kResumeDecodingHeader:
      return out << "kResumeDecodingHeader";
    case Http2FrameDecoder::State::kResumeDecodingPayload:
      return out << "kResumeDecodingPayload";
    case Http2FrameDecoder::State::kDiscardPayload:
      return out << "kDiscardPayload";
  }
  // Since the value doesn't come over the wire, only a programming bug should
  // result in reaching this point.
  int unknown = static_cast<int>(v);
  HTTP2_BUG << "Http2FrameDecoder::State " << unknown;
  return out << "Http2FrameDecoder::State(" << unknown << ")";
}

Http2FrameDecoder::Http2FrameDecoder(Http2FrameDecoderListener* listener)
    : state_(State::kStartDecodingHeader),
      maximum_payload_size_(Http2SettingsInfo::DefaultMaxFrameSize()) {
  set_listener(listener);
}

void Http2FrameDecoder::set_listener(Http2FrameDecoderListener* listener) {
  if (listener == nullptr) {
    listener = &no_op_listener_;
  }
  frame_decoder_state_.set_listener(listener);
}

Http2FrameDecoderListener* Http2FrameDecoder::listener() const {
  return frame_decoder_state_.listener();
}

DecodeStatus Http2FrameDecoder::DecodeFrame(DecodeBuffer* db) {
  HTTP2_DVLOG(2) << "Http2FrameDecoder::DecodeFrame state=" << state_;
  switch (state_) {
    case State::kStartDecodingHeader:
      if (frame_decoder_state_.StartDecodingFrameHeader(db)) {
        return StartDecodingPayload(db);
      }
      state_ = State::kResumeDecodingHeader;
      return DecodeStatus::kDecodeInProgress;

    case State::kResumeDecodingHeader:
      if (frame_decoder_state_.ResumeDecodingFrameHeader(db)) {
        return StartDecodingPayload(db);
      }
      return DecodeStatus::kDecodeInProgress;

    case State::kResumeDecodingPayload:
      return ResumeDecodingPayload(db);

    case State::kDiscardPayload:
      return DiscardPayload(db);
  }

  HTTP2_UNREACHABLE();
  return DecodeStatus::kDecodeError;
}

size_t Http2FrameDecoder::remaining_payload() const {
  return frame_decoder_state_.remaining_payload();
}

uint32_t Http2FrameDecoder::remaining_padding() const {
  return frame_decoder_state_.remaining_padding();
}

DecodeStatus Http2FrameDecoder::StartDecodingPayload(DecodeBuffer* db) {
  const Http2FrameHeader& header = frame_header();

  // TODO(jamessynge): Remove OnFrameHeader once done with supporting
  // SpdyFramer's exact states.
  if (!listener()->OnFrameHeader(header)) {
    HTTP2_DVLOG(2) << "OnFrameHeader rejected the frame, will discard; header: "
                   << header;
    state_ = State::kDiscardPayload;
    frame_decoder_state_.InitializeRemainders();
    return DecodeStatus::kDecodeError;
  }

  if (header.payload_length > maximum_payload_size_) {
    HTTP2_DVLOG(2) << "Payload length is greater than allowed: "
                   << header.payload_length << " > " << maximum_payload_size_
                   << "\n   header: " << header;
    state_ = State::kDiscardPayload;
    frame_decoder_state_.InitializeRemainders();
    listener()->OnFrameSizeError(header);
    return DecodeStatus::kDecodeError;
  }

  // The decode buffer can extend across many frames. Make sure that the
  // buffer we pass to the start method that is specific to the frame type
  // does not exend beyond this frame.
  DecodeBufferSubset subset(db, header.payload_length);
  DecodeStatus status;
  switch (header.type) {
    case Http2FrameType::DATA:
      status = StartDecodingDataPayload(&subset);
      break;

    case Http2FrameType::HEADERS:
      status = StartDecodingHeadersPayload(&subset);
      break;

    case Http2FrameType::PRIORITY:
      status = StartDecodingPriorityPayload(&subset);
      break;

    case Http2FrameType::RST_STREAM:
      status = StartDecodingRstStreamPayload(&subset);
      break;

    case Http2FrameType::SETTINGS:
      status = StartDecodingSettingsPayload(&subset);
      break;

    case Http2FrameType::PUSH_PROMISE:
      status = StartDecodingPushPromisePayload(&subset);
      break;

    case Http2FrameType::PING:
      status = StartDecodingPingPayload(&subset);
      break;

    case Http2FrameType::GOAWAY:
      status = StartDecodingGoAwayPayload(&subset);
      break;

    case Http2FrameType::WINDOW_UPDATE:
      status = StartDecodingWindowUpdatePayload(&subset);
      break;

    case Http2FrameType::CONTINUATION:
      status = StartDecodingContinuationPayload(&subset);
      break;

    case Http2FrameType::ALTSVC:
      status = StartDecodingAltSvcPayload(&subset);
      break;

    default:
      status = StartDecodingUnknownPayload(&subset);
      break;
  }

  if (status == DecodeStatus::kDecodeDone) {
    state_ = State::kStartDecodingHeader;
    return status;
  } else if (status == DecodeStatus::kDecodeInProgress) {
    state_ = State::kResumeDecodingPayload;
    return status;
  } else {
    state_ = State::kDiscardPayload;
    return status;
  }
}

DecodeStatus Http2FrameDecoder::ResumeDecodingPayload(DecodeBuffer* db) {
  // The decode buffer can extend across many frames. Make sure that the
  // buffer we pass to the start method that is specific to the frame type
  // does not exend beyond this frame.
  size_t remaining = frame_decoder_state_.remaining_total_payload();
  DCHECK_LE(remaining, frame_header().payload_length);
  DecodeBufferSubset subset(db, remaining);
  DecodeStatus status;
  switch (frame_header().type) {
    case Http2FrameType::DATA:
      status = ResumeDecodingDataPayload(&subset);
      break;

    case Http2FrameType::HEADERS:
      status = ResumeDecodingHeadersPayload(&subset);
      break;

    case Http2FrameType::PRIORITY:
      status = ResumeDecodingPriorityPayload(&subset);
      break;

    case Http2FrameType::RST_STREAM:
      status = ResumeDecodingRstStreamPayload(&subset);
      break;

    case Http2FrameType::SETTINGS:
      status = ResumeDecodingSettingsPayload(&subset);
      break;

    case Http2FrameType::PUSH_PROMISE:
      status = ResumeDecodingPushPromisePayload(&subset);
      break;

    case Http2FrameType::PING:
      status = ResumeDecodingPingPayload(&subset);
      break;

    case Http2FrameType::GOAWAY:
      status = ResumeDecodingGoAwayPayload(&subset);
      break;

    case Http2FrameType::WINDOW_UPDATE:
      status = ResumeDecodingWindowUpdatePayload(&subset);
      break;

    case Http2FrameType::CONTINUATION:
      status = ResumeDecodingContinuationPayload(&subset);
      break;

    case Http2FrameType::ALTSVC:
      status = ResumeDecodingAltSvcPayload(&subset);
      break;

    default:
      status = ResumeDecodingUnknownPayload(&subset);
      break;
  }

  if (status == DecodeStatus::kDecodeDone) {
    state_ = State::kStartDecodingHeader;
    return status;
  } else if (status == DecodeStatus::kDecodeInProgress) {
    return status;
  } else {
    state_ = State::kDiscardPayload;
    return status;
  }
}

// Clear any of the flags in the frame header that aren't set in valid_flags.
void Http2FrameDecoder::RetainFlags(uint8_t valid_flags) {
  frame_decoder_state_.RetainFlags(valid_flags);
}

// Clear all of the flags in the frame header; for use with frame types that
// don't define any flags, such as WINDOW_UPDATE.
void Http2FrameDecoder::ClearFlags() {
  frame_decoder_state_.ClearFlags();
}

DecodeStatus Http2FrameDecoder::StartDecodingAltSvcPayload(DecodeBuffer* db) {
  ClearFlags();
  return altsvc_payload_decoder_.StartDecodingPayload(&frame_decoder_state_,
                                                      db);
}
DecodeStatus Http2FrameDecoder::ResumeDecodingAltSvcPayload(DecodeBuffer* db) {
  // The frame is not paddable.
  DCHECK_EQ(frame_decoder_state_.remaining_total_payload(),
            frame_decoder_state_.remaining_payload());
  return altsvc_payload_decoder_.ResumeDecodingPayload(&frame_decoder_state_,
                                                       db);
}

DecodeStatus Http2FrameDecoder::StartDecodingContinuationPayload(
    DecodeBuffer* db) {
  RetainFlags(Http2FrameFlag::END_HEADERS);
  return continuation_payload_decoder_.StartDecodingPayload(
      &frame_decoder_state_, db);
}
DecodeStatus Http2FrameDecoder::ResumeDecodingContinuationPayload(
    DecodeBuffer* db) {
  // The frame is not paddable.
  DCHECK_EQ(frame_decoder_state_.remaining_total_payload(),
            frame_decoder_state_.remaining_payload());
  return continuation_payload_decoder_.ResumeDecodingPayload(
      &frame_decoder_state_, db);
}

DecodeStatus Http2FrameDecoder::StartDecodingDataPayload(DecodeBuffer* db) {
  RetainFlags(Http2FrameFlag::END_STREAM | Http2FrameFlag::PADDED);
  return data_payload_decoder_.StartDecodingPayload(&frame_decoder_state_, db);
}
DecodeStatus Http2FrameDecoder::ResumeDecodingDataPayload(DecodeBuffer* db) {
  return data_payload_decoder_.ResumeDecodingPayload(&frame_decoder_state_, db);
}

DecodeStatus Http2FrameDecoder::StartDecodingGoAwayPayload(DecodeBuffer* db) {
  ClearFlags();
  return goaway_payload_decoder_.StartDecodingPayload(&frame_decoder_state_,
                                                      db);
}
DecodeStatus Http2FrameDecoder::ResumeDecodingGoAwayPayload(DecodeBuffer* db) {
  // The frame is not paddable.
  DCHECK_EQ(frame_decoder_state_.remaining_total_payload(),
            frame_decoder_state_.remaining_payload());
  return goaway_payload_decoder_.ResumeDecodingPayload(&frame_decoder_state_,
                                                       db);
}

DecodeStatus Http2FrameDecoder::StartDecodingHeadersPayload(DecodeBuffer* db) {
  RetainFlags(Http2FrameFlag::END_STREAM | Http2FrameFlag::END_HEADERS |
              Http2FrameFlag::PADDED | Http2FrameFlag::PRIORITY);
  return headers_payload_decoder_.StartDecodingPayload(&frame_decoder_state_,
                                                       db);
}
DecodeStatus Http2FrameDecoder::ResumeDecodingHeadersPayload(DecodeBuffer* db) {
  DCHECK_LE(frame_decoder_state_.remaining_payload_and_padding(),
            frame_header().payload_length);
  return headers_payload_decoder_.ResumeDecodingPayload(&frame_decoder_state_,
                                                        db);
}

DecodeStatus Http2FrameDecoder::StartDecodingPingPayload(DecodeBuffer* db) {
  RetainFlags(Http2FrameFlag::ACK);
  return ping_payload_decoder_.StartDecodingPayload(&frame_decoder_state_, db);
}
DecodeStatus Http2FrameDecoder::ResumeDecodingPingPayload(DecodeBuffer* db) {
  // The frame is not paddable.
  DCHECK_EQ(frame_decoder_state_.remaining_total_payload(),
            frame_decoder_state_.remaining_payload());
  return ping_payload_decoder_.ResumeDecodingPayload(&frame_decoder_state_, db);
}

DecodeStatus Http2FrameDecoder::StartDecodingPriorityPayload(DecodeBuffer* db) {
  ClearFlags();
  return priority_payload_decoder_.StartDecodingPayload(&frame_decoder_state_,
                                                        db);
}
DecodeStatus Http2FrameDecoder::ResumeDecodingPriorityPayload(
    DecodeBuffer* db) {
  // The frame is not paddable.
  DCHECK_EQ(frame_decoder_state_.remaining_total_payload(),
            frame_decoder_state_.remaining_payload());
  return priority_payload_decoder_.ResumeDecodingPayload(&frame_decoder_state_,
                                                         db);
}

DecodeStatus Http2FrameDecoder::StartDecodingPushPromisePayload(
    DecodeBuffer* db) {
  RetainFlags(Http2FrameFlag::END_HEADERS | Http2FrameFlag::PADDED);
  return push_promise_payload_decoder_.StartDecodingPayload(
      &frame_decoder_state_, db);
}
DecodeStatus Http2FrameDecoder::ResumeDecodingPushPromisePayload(
    DecodeBuffer* db) {
  DCHECK_LE(frame_decoder_state_.remaining_payload_and_padding(),
            frame_header().payload_length);
  return push_promise_payload_decoder_.ResumeDecodingPayload(
      &frame_decoder_state_, db);
}

DecodeStatus Http2FrameDecoder::StartDecodingRstStreamPayload(
    DecodeBuffer* db) {
  ClearFlags();
  return rst_stream_payload_decoder_.StartDecodingPayload(&frame_decoder_state_,
                                                          db);
}
DecodeStatus Http2FrameDecoder::ResumeDecodingRstStreamPayload(
    DecodeBuffer* db) {
  // The frame is not paddable.
  DCHECK_EQ(frame_decoder_state_.remaining_total_payload(),
            frame_decoder_state_.remaining_payload());
  return rst_stream_payload_decoder_.ResumeDecodingPayload(
      &frame_decoder_state_, db);
}

DecodeStatus Http2FrameDecoder::StartDecodingSettingsPayload(DecodeBuffer* db) {
  RetainFlags(Http2FrameFlag::ACK);
  return settings_payload_decoder_.StartDecodingPayload(&frame_decoder_state_,
                                                        db);
}
DecodeStatus Http2FrameDecoder::ResumeDecodingSettingsPayload(
    DecodeBuffer* db) {
  // The frame is not paddable.
  DCHECK_EQ(frame_decoder_state_.remaining_total_payload(),
            frame_decoder_state_.remaining_payload());
  return settings_payload_decoder_.ResumeDecodingPayload(&frame_decoder_state_,
                                                         db);
}

DecodeStatus Http2FrameDecoder::StartDecodingUnknownPayload(DecodeBuffer* db) {
  // We don't known what type of frame this is, so we don't know which flags
  // are valid, so we don't touch them.
  return unknown_payload_decoder_.StartDecodingPayload(&frame_decoder_state_,
                                                       db);
}
DecodeStatus Http2FrameDecoder::ResumeDecodingUnknownPayload(DecodeBuffer* db) {
  // We don't known what type of frame this is, so we treat it as not paddable.
  DCHECK_EQ(frame_decoder_state_.remaining_total_payload(),
            frame_decoder_state_.remaining_payload());
  return unknown_payload_decoder_.ResumeDecodingPayload(&frame_decoder_state_,
                                                        db);
}

DecodeStatus Http2FrameDecoder::StartDecodingWindowUpdatePayload(
    DecodeBuffer* db) {
  ClearFlags();
  return window_update_payload_decoder_.StartDecodingPayload(
      &frame_decoder_state_, db);
}
DecodeStatus Http2FrameDecoder::ResumeDecodingWindowUpdatePayload(
    DecodeBuffer* db) {
  // The frame is not paddable.
  DCHECK_EQ(frame_decoder_state_.remaining_total_payload(),
            frame_decoder_state_.remaining_payload());
  return window_update_payload_decoder_.ResumeDecodingPayload(
      &frame_decoder_state_, db);
}

DecodeStatus Http2FrameDecoder::DiscardPayload(DecodeBuffer* db) {
  HTTP2_DVLOG(2) << "remaining_payload="
                 << frame_decoder_state_.remaining_payload_
                 << "; remaining_padding="
                 << frame_decoder_state_.remaining_padding_;
  frame_decoder_state_.remaining_payload_ +=
      frame_decoder_state_.remaining_padding_;
  frame_decoder_state_.remaining_padding_ = 0;
  const size_t avail = frame_decoder_state_.AvailablePayload(db);
  HTTP2_DVLOG(2) << "avail=" << avail;
  if (avail > 0) {
    frame_decoder_state_.ConsumePayload(avail);
    db->AdvanceCursor(avail);
  }
  if (frame_decoder_state_.remaining_payload_ == 0) {
    state_ = State::kStartDecodingHeader;
    return DecodeStatus::kDecodeDone;
  }
  return DecodeStatus::kDecodeInProgress;
}

}  // namespace http2
