// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/quic/http/decoder/quic_http_frame_decoder_state.h"

namespace net {

QuicHttpDecodeStatus QuicHttpFrameDecoderState::ReadPadLength(
    QuicHttpDecodeBuffer* db,
    bool report_pad_length) {
  DVLOG(2) << "ReadPadLength db->Remaining=" << db->Remaining()
           << "; payload_length=" << frame_header().payload_length;
  DCHECK(IsPaddable());
  DCHECK(frame_header().IsPadded());

  // Pad Length is always at the start of the frame, so remaining_payload_
  // should equal payload_length at this point.
  const uint32_t total_payload = frame_header().payload_length;
  DCHECK_EQ(total_payload, remaining_payload_);
  DCHECK_EQ(0u, remaining_padding_);

  if (db->HasData()) {
    const uint32_t pad_length = db->DecodeUInt8();
    const uint32_t total_padding = pad_length + 1;
    if (total_padding <= total_payload) {
      remaining_padding_ = pad_length;
      remaining_payload_ = total_payload - total_padding;
      if (report_pad_length) {
        listener()->OnPadLength(pad_length);
      }
      return QuicHttpDecodeStatus::kDecodeDone;
    }
    const uint32_t missing_length = total_padding - total_payload;
    // To allow for the possibility of recovery, record the number of
    // remaining bytes of the frame's payload (invalid though it is)
    // in remaining_payload_.
    remaining_payload_ = total_payload - 1;  // 1 for sizeof(Pad Length).
    remaining_padding_ = 0;
    listener()->OnPaddingTooLong(frame_header(), missing_length);
    return QuicHttpDecodeStatus::kDecodeError;
  }

  if (total_payload == 0) {
    remaining_payload_ = 0;
    remaining_padding_ = 0;
    listener()->OnPaddingTooLong(frame_header(), 1);
    return QuicHttpDecodeStatus::kDecodeError;
  }
  // Need to wait for another buffer.
  return QuicHttpDecodeStatus::kDecodeInProgress;
}

bool QuicHttpFrameDecoderState::SkipPadding(QuicHttpDecodeBuffer* db) {
  DVLOG(2) << "SkipPadding remaining_padding_=" << remaining_padding_
           << ", db->Remaining=" << db->Remaining()
           << ", header: " << frame_header();
  DCHECK_EQ(remaining_payload_, 0u);
  DCHECK(IsPaddable()) << "header: " << frame_header();
  DCHECK_GE(remaining_padding_, 0u);
  DCHECK(remaining_padding_ == 0 || frame_header().IsPadded())
      << "remaining_padding_=" << remaining_padding_
      << ", header: " << frame_header();
  const size_t avail = AvailablePadding(db);
  if (avail > 0) {
    listener()->OnPadding(db->cursor(), avail);
    db->AdvanceCursor(avail);
    remaining_padding_ -= avail;
  }
  return remaining_padding_ == 0;
}

QuicHttpDecodeStatus QuicHttpFrameDecoderState::ReportFrameSizeError() {
  DVLOG(2) << "QuicHttpFrameDecoderState::ReportFrameSizeError: "
           << " remaining_payload_=" << remaining_payload_
           << "; remaining_padding_=" << remaining_padding_
           << ", header: " << frame_header();
  listener()->OnFrameSizeError(frame_header());
  return QuicHttpDecodeStatus::kDecodeError;
}

}  // namespace net
