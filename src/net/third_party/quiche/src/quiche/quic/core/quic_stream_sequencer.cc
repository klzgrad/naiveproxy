// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "quiche/quic/core/quic_stream_sequencer.h"

#include <algorithm>
#include <cstddef>
#include <limits>
#include <string>
#include <utility>

#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"
#include "quiche/quic/core/quic_clock.h"
#include "quiche/quic/core/quic_error_codes.h"
#include "quiche/quic/core/quic_packets.h"
#include "quiche/quic/core/quic_stream.h"
#include "quiche/quic/core/quic_stream_sequencer_buffer.h"
#include "quiche/quic/core/quic_types.h"
#include "quiche/quic/core/quic_utils.h"
#include "quiche/quic/platform/api/quic_bug_tracker.h"
#include "quiche/quic/platform/api/quic_flag_utils.h"
#include "quiche/quic/platform/api/quic_flags.h"
#include "quiche/quic/platform/api/quic_logging.h"
#include "quiche/quic/platform/api/quic_stack_trace.h"

namespace quic {

QuicStreamSequencer::QuicStreamSequencer(StreamInterface* quic_stream)
    : stream_(quic_stream),
      buffered_frames_(kStreamReceiveWindowLimit),
      highest_offset_(0),
      close_offset_(std::numeric_limits<QuicStreamOffset>::max()),
      reliable_offset_(0),
      blocked_(false),
      num_frames_received_(0),
      num_duplicate_frames_received_(0),
      ignore_read_data_(false),
      level_triggered_(false) {}

QuicStreamSequencer::~QuicStreamSequencer() {
  if (stream_ == nullptr) {
    QUIC_BUG(quic_bug_10858_1) << "Double free'ing QuicStreamSequencer at "
                               << this << ". " << QuicStackTrace();
  }
  stream_ = nullptr;
}

void QuicStreamSequencer::OnStreamFrame(const QuicStreamFrame& frame) {
  QUICHE_DCHECK_LE(frame.offset + frame.data_length, close_offset_);
  ++num_frames_received_;
  const QuicStreamOffset byte_offset = frame.offset;
  const size_t data_len = frame.data_length;

  if (frame.fin &&
      (!CloseStreamAtOffset(frame.offset + data_len) || data_len == 0)) {
    return;
  }
  if (stream_->version().HasIetfQuicFrames() && data_len == 0) {
    QUICHE_DCHECK(!frame.fin);
    // Ignore empty frame with no fin.
    return;
  }
  OnFrameData(byte_offset, data_len, frame.data_buffer);
}

void QuicStreamSequencer::OnCryptoFrame(const QuicCryptoFrame& frame) {
  ++num_frames_received_;
  if (frame.data_length == 0) {
    // Ignore empty crypto frame.
    return;
  }
  OnFrameData(frame.offset, frame.data_length, frame.data_buffer);
}

void QuicStreamSequencer::OnReliableReset(QuicStreamOffset reliable_size) {
  reliable_offset_ = reliable_size;
}

void QuicStreamSequencer::OnFrameData(QuicStreamOffset byte_offset,
                                      size_t data_len,
                                      const char* data_buffer) {
  highest_offset_ = std::max(highest_offset_, byte_offset + data_len);
  const size_t previous_readable_bytes = buffered_frames_.ReadableBytes();
  size_t bytes_written;
  std::string error_details;
  QuicErrorCode result = buffered_frames_.OnStreamData(
      byte_offset, absl::string_view(data_buffer, data_len), &bytes_written,
      &error_details);
  if (result != QUIC_NO_ERROR) {
    std::string details =
        absl::StrCat("Stream ", stream_->id(), ": ",
                     QuicErrorCodeToString(result), ": ", error_details);
    QUIC_LOG_FIRST_N(WARNING, 50) << QuicErrorCodeToString(result);
    QUIC_LOG_FIRST_N(WARNING, 50) << details;
    stream_->OnUnrecoverableError(result, details);
    return;
  }

  if (bytes_written == 0) {
    ++num_duplicate_frames_received_;
    // Silently ignore duplicates.
    return;
  }

  if (blocked_) {
    return;
  }

  if (level_triggered_) {
    if (buffered_frames_.ReadableBytes() > previous_readable_bytes) {
      // Readable bytes has changed, let stream decide if to inform application
      // or not.
      if (ignore_read_data_) {
        FlushBufferedFrames();
      } else {
        stream_->OnDataAvailable();
      }
    }
    return;
  }
  const bool stream_unblocked =
      previous_readable_bytes == 0 && buffered_frames_.ReadableBytes() > 0;
  if (stream_unblocked) {
    if (ignore_read_data_) {
      FlushBufferedFrames();
    } else {
      stream_->OnDataAvailable();
    }
  }
}

bool QuicStreamSequencer::CloseStreamAtOffset(QuicStreamOffset offset) {
  const QuicStreamOffset kMaxOffset =
      std::numeric_limits<QuicStreamOffset>::max();

  // If there is a scheduled close, the new offset should match it.
  if (close_offset_ != kMaxOffset && offset != close_offset_) {
    stream_->OnUnrecoverableError(
        QUIC_STREAM_SEQUENCER_INVALID_STATE,
        absl::StrCat(
            "Stream ", stream_->id(), " received new final offset: ", offset,
            ", which is different from close offset: ", close_offset_));
    return false;
  }

  // The final offset should be no less than the highest offset that is
  // received.
  if (offset < highest_offset_) {
    stream_->OnUnrecoverableError(
        QUIC_STREAM_SEQUENCER_INVALID_STATE,
        absl::StrCat(
            "Stream ", stream_->id(), " received fin with offset: ", offset,
            ", which reduces current highest offset: ", highest_offset_));
    return false;
  }

  if (offset < reliable_offset_) {
    stream_->OnUnrecoverableError(
        QUIC_STREAM_MULTIPLE_OFFSET,
        absl::StrCat(
            "Stream ", stream_->id(), " received fin with offset: ", offset,
            ", which reduces current reliable offset: ", reliable_offset_));
    return false;
  }

  close_offset_ = offset;

  MaybeCloseStream();
  return true;
}

void QuicStreamSequencer::MaybeCloseStream() {
  if (blocked_ || !IsClosed()) {
    return;
  }

  QUIC_DVLOG(1) << "Passing up termination, as we've processed "
                << buffered_frames_.BytesConsumed() << " of " << close_offset_
                << " bytes.";
  // This will cause the stream to consume the FIN.
  // Technically it's an error if |num_bytes_consumed| isn't exactly
  // equal to |close_offset|, but error handling seems silly at this point.
  if (ignore_read_data_) {
    // The sequencer is discarding stream data and must notify the stream on
    // receipt of a FIN because the consumer won't.
    stream_->OnFinRead();
  } else {
    stream_->OnDataAvailable();
  }
  buffered_frames_.Clear();
}

int QuicStreamSequencer::GetReadableRegions(iovec* iov, size_t iov_len) const {
  QUICHE_DCHECK(!blocked_);
  return buffered_frames_.GetReadableRegions(iov, iov_len);
}

bool QuicStreamSequencer::GetReadableRegion(iovec* iov) const {
  QUICHE_DCHECK(!blocked_);
  return buffered_frames_.GetReadableRegion(iov);
}

bool QuicStreamSequencer::PeekRegion(QuicStreamOffset offset,
                                     iovec* iov) const {
  QUICHE_DCHECK(!blocked_);
  return buffered_frames_.PeekRegion(offset, iov);
}

void QuicStreamSequencer::Read(std::string* buffer) {
  QUICHE_DCHECK(!blocked_);
  buffer->resize(buffer->size() + ReadableBytes());
  iovec iov;
  iov.iov_len = ReadableBytes();
  iov.iov_base = &(*buffer)[buffer->size() - iov.iov_len];
  Readv(&iov, 1);
}

size_t QuicStreamSequencer::Readv(const struct iovec* iov, size_t iov_len) {
  QUICHE_DCHECK(!blocked_);
  std::string error_details;
  size_t bytes_read;
  QuicErrorCode read_error =
      buffered_frames_.Readv(iov, iov_len, &bytes_read, &error_details);
  if (read_error != QUIC_NO_ERROR) {
    std::string details =
        absl::StrCat("Stream ", stream_->id(), ": ", error_details);
    stream_->OnUnrecoverableError(read_error, details);
    return bytes_read;
  }

  stream_->AddBytesConsumed(bytes_read);
  return bytes_read;
}

bool QuicStreamSequencer::HasBytesToRead() const {
  return buffered_frames_.HasBytesToRead();
}

size_t QuicStreamSequencer::ReadableBytes() const {
  return buffered_frames_.ReadableBytes();
}

bool QuicStreamSequencer::IsClosed() const {
  return buffered_frames_.BytesConsumed() >= close_offset_;
}

void QuicStreamSequencer::MarkConsumed(size_t num_bytes_consumed) {
  QUICHE_DCHECK(!blocked_);
  bool result = buffered_frames_.MarkConsumed(num_bytes_consumed);
  if (!result) {
    QUIC_BUG(quic_bug_10858_2)
        << "Invalid argument to MarkConsumed."
        << " expect to consume: " << num_bytes_consumed
        << ", but not enough bytes available. " << DebugString();
    stream_->ResetWithError(
        QuicResetStreamError::FromInternal(QUIC_ERROR_PROCESSING_STREAM));
    return;
  }
  stream_->AddBytesConsumed(num_bytes_consumed);
}

void QuicStreamSequencer::SetBlockedUntilFlush() { blocked_ = true; }

void QuicStreamSequencer::SetUnblocked() {
  blocked_ = false;
  if (IsClosed() || HasBytesToRead()) {
    stream_->OnDataAvailable();
  }
}

void QuicStreamSequencer::StopReading() {
  if (ignore_read_data_) {
    return;
  }
  ignore_read_data_ = true;
  FlushBufferedFrames();
}

void QuicStreamSequencer::ReleaseBuffer() {
  buffered_frames_.ReleaseWholeBuffer();
}

void QuicStreamSequencer::ReleaseBufferIfEmpty() {
  if (buffered_frames_.Empty()) {
    buffered_frames_.ReleaseWholeBuffer();
  }
}

void QuicStreamSequencer::FlushBufferedFrames() {
  QUICHE_DCHECK(ignore_read_data_);
  size_t bytes_flushed = buffered_frames_.FlushBufferedFrames();
  QUIC_DVLOG(1) << "Flushing buffered data at offset "
                << buffered_frames_.BytesConsumed() << " length "
                << bytes_flushed << " for stream " << stream_->id();
  stream_->AddBytesConsumed(bytes_flushed);
  MaybeCloseStream();
}

size_t QuicStreamSequencer::NumBytesBuffered() const {
  return buffered_frames_.BytesBuffered();
}

QuicStreamOffset QuicStreamSequencer::NumBytesConsumed() const {
  return buffered_frames_.BytesConsumed();
}

bool QuicStreamSequencer::IsAllDataAvailable() const {
  QUICHE_DCHECK_LE(NumBytesConsumed() + NumBytesBuffered(), close_offset_);
  return NumBytesConsumed() + NumBytesBuffered() >= close_offset_;
}

std::string QuicStreamSequencer::DebugString() const {
  // clang-format off
  return absl::StrCat(
      "QuicStreamSequencer:  bytes buffered: ", NumBytesBuffered(),
      "\n  bytes consumed: ", NumBytesConsumed(),
      "\n  first missing byte: ", buffered_frames_.FirstMissingByte(),
      "\n  next expected byte: ", buffered_frames_.NextExpectedByte(),
      "\n  received frames: ", buffered_frames_.ReceivedFramesDebugString(),
      "\n  has bytes to read: ", HasBytesToRead() ? "true" : "false",
      "\n  frames received: ", num_frames_received(),
      "\n  close offset bytes: ", close_offset_,
      "\n  is closed: ", IsClosed() ? "true" : "false");
  // clang-format on
}

}  // namespace quic
