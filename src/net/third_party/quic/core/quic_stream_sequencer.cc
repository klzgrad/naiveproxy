// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/third_party/quic/core/quic_stream_sequencer.h"

#include <algorithm>
#include <limits>
#include <utility>

#include "net/third_party/quic/core/quic_packets.h"
#include "net/third_party/quic/core/quic_stream.h"
#include "net/third_party/quic/core/quic_stream_sequencer_buffer.h"
#include "net/third_party/quic/core/quic_utils.h"
#include "net/third_party/quic/platform/api/quic_bug_tracker.h"
#include "net/third_party/quic/platform/api/quic_clock.h"
#include "net/third_party/quic/platform/api/quic_flag_utils.h"
#include "net/third_party/quic/platform/api/quic_logging.h"
#include "net/third_party/quic/platform/api/quic_str_cat.h"
#include "net/third_party/quic/platform/api/quic_string.h"
#include "net/third_party/quic/platform/api/quic_string_piece.h"

namespace quic {

QuicStreamSequencer::QuicStreamSequencer(StreamInterface* quic_stream)
    : stream_(quic_stream),
      buffered_frames_(kStreamReceiveWindowLimit),
      close_offset_(std::numeric_limits<QuicStreamOffset>::max()),
      blocked_(false),
      num_frames_received_(0),
      num_duplicate_frames_received_(0),
      ignore_read_data_(false),
      level_triggered_(false),
      stop_reading_when_level_triggered_(
          GetQuicReloadableFlag(quic_stop_reading_when_level_triggered)) {}

QuicStreamSequencer::~QuicStreamSequencer() {}

void QuicStreamSequencer::OnStreamFrame(const QuicStreamFrame& frame) {
  ++num_frames_received_;
  const QuicStreamOffset byte_offset = frame.offset;
  const size_t data_len = frame.data_length;

  if (frame.fin) {
    CloseStreamAtOffset(frame.offset + data_len);
    if (data_len == 0) {
      return;
    }
  }
  const size_t previous_readable_bytes = buffered_frames_.ReadableBytes();
  size_t bytes_written;
  QuicString error_details;
  QuicErrorCode result = buffered_frames_.OnStreamData(
      byte_offset, QuicStringPiece(frame.data_buffer, frame.data_length),
      &bytes_written, &error_details);
  if (result != QUIC_NO_ERROR) {
    QuicString details = QuicStrCat(
        "Stream ", stream_->id(), ": ", QuicErrorCodeToString(result), ": ",
        error_details,
        "\nPeer Address: ", stream_->PeerAddressOfLatestPacket().ToString());
    QUIC_LOG_FIRST_N(WARNING, 50) << QuicErrorCodeToString(result);
    QUIC_LOG_FIRST_N(WARNING, 50) << details;
    stream_->CloseConnectionWithDetails(result, details);
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
      if (stop_reading_when_level_triggered_ && ignore_read_data_) {
        QUIC_FLAG_COUNT(
            quic_reloadable_flag_quic_stop_reading_when_level_triggered);
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

void QuicStreamSequencer::CloseStreamAtOffset(QuicStreamOffset offset) {
  const QuicStreamOffset kMaxOffset =
      std::numeric_limits<QuicStreamOffset>::max();

  // If there is a scheduled close, the new offset should match it.
  if (close_offset_ != kMaxOffset && offset != close_offset_) {
    stream_->Reset(QUIC_MULTIPLE_TERMINATION_OFFSETS);
    return;
  }

  close_offset_ = offset;

  MaybeCloseStream();
}

bool QuicStreamSequencer::MaybeCloseStream() {
  if (blocked_ || !IsClosed()) {
    return false;
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
  return true;
}

int QuicStreamSequencer::GetReadableRegions(iovec* iov, size_t iov_len) const {
  DCHECK(!blocked_);
  return buffered_frames_.GetReadableRegions(iov, iov_len);
}

bool QuicStreamSequencer::GetReadableRegion(iovec* iov) const {
  DCHECK(!blocked_);
  return buffered_frames_.GetReadableRegion(iov);
}

void QuicStreamSequencer::Read(QuicString* buffer) {
  DCHECK(!blocked_);
  buffer->resize(buffer->size() + ReadableBytes());
  iovec iov;
  iov.iov_len = ReadableBytes();
  iov.iov_base = &(*buffer)[buffer->size() - iov.iov_len];
  Readv(&iov, 1);
}

int QuicStreamSequencer::Readv(const struct iovec* iov, size_t iov_len) {
  DCHECK(!blocked_);
  QuicString error_details;
  size_t bytes_read;
  QuicErrorCode read_error =
      buffered_frames_.Readv(iov, iov_len, &bytes_read, &error_details);
  if (read_error != QUIC_NO_ERROR) {
    QuicString details =
        QuicStrCat("Stream ", stream_->id(), ": ", error_details);
    stream_->CloseConnectionWithDetails(read_error, details);
    return static_cast<int>(bytes_read);
  }

  stream_->AddBytesConsumed(bytes_read);
  return static_cast<int>(bytes_read);
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
  DCHECK(!blocked_);
  bool result = buffered_frames_.MarkConsumed(num_bytes_consumed);
  if (!result) {
    QUIC_BUG << "Invalid argument to MarkConsumed."
             << " expect to consume: " << num_bytes_consumed
             << ", but not enough bytes available. " << DebugString();
    stream_->Reset(QUIC_ERROR_PROCESSING_STREAM);
    return;
  }
  stream_->AddBytesConsumed(num_bytes_consumed);
}

void QuicStreamSequencer::SetBlockedUntilFlush() {
  blocked_ = true;
}

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
  DCHECK(ignore_read_data_);
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

const QuicString QuicStreamSequencer::DebugString() const {
  // clang-format off
  return QuicStrCat("QuicStreamSequencer:",
                "\n  bytes buffered: ", NumBytesBuffered(),
                "\n  bytes consumed: ", NumBytesConsumed(),
                "\n  has bytes to read: ", HasBytesToRead() ? "true" : "false",
                "\n  frames received: ", num_frames_received(),
                "\n  close offset bytes: ", close_offset_,
                "\n  is closed: ", IsClosed() ? "true" : "false");
  // clang-format on
}

}  // namespace quic
