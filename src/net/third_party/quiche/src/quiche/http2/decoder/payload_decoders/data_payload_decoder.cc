// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "quiche/http2/decoder/payload_decoders/data_payload_decoder.h"

#include <stddef.h>

#include <ostream>

#include "absl/base/macros.h"
#include "quiche/http2/core/http2_constants.h"
#include "quiche/http2/core/http2_structures.h"
#include "quiche/http2/decoder/decode_buffer.h"
#include "quiche/http2/decoder/http2_frame_decoder_listener.h"
#include "quiche/common/platform/api/quiche_bug_tracker.h"
#include "quiche/common/platform/api/quiche_logging.h"

namespace http2 {

std::ostream& operator<<(std::ostream& out,
                         DataPayloadDecoder::PayloadState v) {
  switch (v) {
    case DataPayloadDecoder::PayloadState::kReadPadLength:
      return out << "kReadPadLength";
    case DataPayloadDecoder::PayloadState::kReadPayload:
      return out << "kReadPayload";
    case DataPayloadDecoder::PayloadState::kSkipPadding:
      return out << "kSkipPadding";
  }
  // Since the value doesn't come over the wire, only a programming bug should
  // result in reaching this point.
  int unknown = static_cast<int>(v);
  QUICHE_BUG(http2_bug_174_1)
      << "Invalid DataPayloadDecoder::PayloadState: " << unknown;
  return out << "DataPayloadDecoder::PayloadState(" << unknown << ")";
}

DecodeStatus DataPayloadDecoder::StartDecodingPayload(FrameDecoderState* state,
                                                      DecodeBuffer* db) {
  const Http2FrameHeader& frame_header = state->frame_header();
  const uint32_t total_length = frame_header.payload_length;

  QUICHE_DVLOG(2) << "DataPayloadDecoder::StartDecodingPayload: "
                  << frame_header;
  QUICHE_DCHECK_EQ(Http2FrameType::DATA, frame_header.type);
  QUICHE_DCHECK_LE(db->Remaining(), total_length);
  QUICHE_DCHECK_EQ(0, frame_header.flags & ~(Http2FrameFlag::END_STREAM |
                                             Http2FrameFlag::PADDED));

  // Special case for the hoped for common case: unpadded and fits fully into
  // the decode buffer. TO BE SEEN if that is true. It certainly requires that
  // the transport buffers be large (e.g. >> 16KB typically).
  // TODO(jamessynge) Add counters.
  QUICHE_DVLOG(2) << "StartDecodingPayload total_length=" << total_length;
  if (!frame_header.IsPadded()) {
    QUICHE_DVLOG(2) << "StartDecodingPayload !IsPadded";
    if (db->Remaining() == total_length) {
      QUICHE_DVLOG(2) << "StartDecodingPayload all present";
      // Note that we don't cache the listener field so that the callee can
      // replace it if the frame is bad.
      // If this case is common enough, consider combining the 3 callbacks
      // into one.
      state->listener()->OnDataStart(frame_header);
      if (total_length > 0) {
        state->listener()->OnDataPayload(db->cursor(), total_length);
        db->AdvanceCursor(total_length);
      }
      state->listener()->OnDataEnd();
      return DecodeStatus::kDecodeDone;
    }
    payload_state_ = PayloadState::kReadPayload;
  } else {
    payload_state_ = PayloadState::kReadPadLength;
  }
  state->InitializeRemainders();
  state->listener()->OnDataStart(frame_header);
  return ResumeDecodingPayload(state, db);
}

DecodeStatus DataPayloadDecoder::ResumeDecodingPayload(FrameDecoderState* state,
                                                       DecodeBuffer* db) {
  QUICHE_DVLOG(2) << "DataPayloadDecoder::ResumeDecodingPayload payload_state_="
                  << payload_state_;
  const Http2FrameHeader& frame_header = state->frame_header();
  QUICHE_DCHECK_EQ(Http2FrameType::DATA, frame_header.type);
  QUICHE_DCHECK_LE(state->remaining_payload_and_padding(),
                   frame_header.payload_length);
  QUICHE_DCHECK_LE(db->Remaining(), state->remaining_payload_and_padding());
  DecodeStatus status;
  size_t avail;
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
      ABSL_FALLTHROUGH_INTENDED;

    case PayloadState::kReadPayload:
      avail = state->AvailablePayload(db);
      if (avail > 0) {
        state->listener()->OnDataPayload(db->cursor(), avail);
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
        state->listener()->OnDataEnd();
        return DecodeStatus::kDecodeDone;
      }
      payload_state_ = PayloadState::kSkipPadding;
      return DecodeStatus::kDecodeInProgress;
  }
  QUICHE_BUG(http2_bug_174_2) << "PayloadState: " << payload_state_;
  return DecodeStatus::kDecodeError;
}

}  // namespace http2
