// Copyright (c) 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "quiche/quic/core/quic_stream_send_buffer.h"

#include <algorithm>
#include <cstddef>
#include <utility>

#include "absl/strings/string_view.h"
#include "absl/types/span.h"
#include "quiche/quic/core/quic_data_writer.h"
#include "quiche/quic/core/quic_interval.h"
#include "quiche/quic/core/quic_interval_set.h"
#include "quiche/quic/core/quic_types.h"
#include "quiche/quic/platform/api/quic_bug_tracker.h"
#include "quiche/quic/platform/api/quic_flags.h"
#include "quiche/quic/platform/api/quic_logging.h"
#include "quiche/common/platform/api/quiche_logging.h"
#include "quiche/common/quiche_buffer_allocator.h"
#include "quiche/common/quiche_mem_slice.h"

namespace quic {

namespace {

struct CompareOffset {
  bool operator()(const BufferedSlice& slice, QuicStreamOffset offset) const {
    return slice.offset + slice.slice.length() < offset;
  }
};

}  // namespace

BufferedSlice::BufferedSlice(quiche::QuicheMemSlice mem_slice,
                             QuicStreamOffset offset)
    : slice(std::move(mem_slice)), offset(offset) {}

BufferedSlice::BufferedSlice(BufferedSlice&& other) = default;

BufferedSlice& BufferedSlice::operator=(BufferedSlice&& other) = default;

BufferedSlice::~BufferedSlice() {}

QuicInterval<std::size_t> BufferedSlice::interval() const {
  const std::size_t length = slice.length();
  return QuicInterval<std::size_t>(offset, offset + length);
}

bool StreamPendingRetransmission::operator==(
    const StreamPendingRetransmission& other) const {
  return offset == other.offset && length == other.length;
}

QuicStreamSendBuffer::QuicStreamSendBuffer(
    quiche::QuicheBufferAllocator* allocator)
    : allocator_(allocator) {}

QuicStreamSendBuffer::~QuicStreamSendBuffer() {}

void QuicStreamSendBuffer::SaveStreamData(absl::string_view data) {
  QUIC_DVLOG(2) << "Save stream data offset " << stream_offset_ << " length "
                << data.length();
  QUICHE_DCHECK(!data.empty());

  // Latch the maximum data slice size.
  const QuicByteCount max_data_slice_size =
      GetQuicFlag(quic_send_buffer_max_data_slice_size);
  while (!data.empty()) {
    auto slice_len = std::min<absl::string_view::size_type>(
        data.length(), max_data_slice_size);
    auto buffer =
        quiche::QuicheBuffer::Copy(allocator_, data.substr(0, slice_len));
    SaveMemSlice(quiche::QuicheMemSlice(std::move(buffer)));

    data = data.substr(slice_len);
  }
}

void QuicStreamSendBuffer::SaveMemSlice(quiche::QuicheMemSlice slice) {
  QUIC_DVLOG(2) << "Save slice offset " << stream_offset_ << " length "
                << slice.length();
  if (slice.empty()) {
    QUIC_BUG(quic_bug_10853_1) << "Try to save empty MemSlice to send buffer.";
    return;
  }
  size_t length = slice.length();
  BufferedSlice bs = BufferedSlice(std::move(slice), stream_offset_);
  interval_deque_.PushBack(std::move(bs));
  stream_offset_ += length;
}

QuicByteCount QuicStreamSendBuffer::SaveMemSliceSpan(
    absl::Span<quiche::QuicheMemSlice> span) {
  QuicByteCount total = 0;
  for (quiche::QuicheMemSlice& slice : span) {
    if (slice.empty()) {
      // Skip empty slices.
      continue;
    }
    total += slice.length();
    SaveMemSlice(std::move(slice));
  }
  return total;
}

void QuicStreamSendBuffer::OnStreamDataConsumed(size_t bytes_consumed) {
  stream_bytes_written_ += bytes_consumed;
  stream_bytes_outstanding_ += bytes_consumed;
}

bool QuicStreamSendBuffer::WriteStreamData(QuicStreamOffset offset,
                                           QuicByteCount data_length,
                                           QuicDataWriter* writer) {
  // The iterator returned from |interval_deque_| will automatically advance
  // the internal write index for the QuicIntervalDeque. The incrementing is
  // done in operator++.
  for (auto slice_it = interval_deque_.DataAt(offset);
       slice_it != interval_deque_.DataEnd(); ++slice_it) {
    if (data_length == 0 || offset < slice_it->offset) {
      break;
    }

    QuicByteCount slice_offset = offset - slice_it->offset;
    QuicByteCount available_bytes_in_slice =
        slice_it->slice.length() - slice_offset;
    QuicByteCount copy_length = std::min(data_length, available_bytes_in_slice);
    if (!writer->WriteBytes(slice_it->slice.data() + slice_offset,
                            copy_length)) {
      QUIC_BUG(quic_bug_10853_2) << "Writer fails to write.";
      return false;
    }
    offset += copy_length;
    data_length -= copy_length;
  }
  return data_length == 0;
}

bool QuicStreamSendBuffer::OnStreamDataAcked(
    QuicStreamOffset offset, QuicByteCount data_length,
    QuicByteCount* newly_acked_length) {
  QUIC_DVLOG(2) << "Marking data acked at offset " << offset << " length "
                << data_length;
  *newly_acked_length = 0;
  if (data_length == 0) {
    return true;
  }
  if (bytes_acked_.Empty() || offset >= bytes_acked_.rbegin()->max() ||
      bytes_acked_.IsDisjoint(
          QuicInterval<QuicStreamOffset>(offset, offset + data_length))) {
    // Optimization for the typical case, when all data is newly acked.
    if (stream_bytes_outstanding_ < data_length) {
      return false;
    }
    bytes_acked_.AddOptimizedForAppend(offset, offset + data_length);
    *newly_acked_length = data_length;
    stream_bytes_outstanding_ -= data_length;
    pending_retransmissions_.Difference(offset, offset + data_length);
    if (!FreeMemSlices(offset, offset + data_length)) {
      return false;
    }
    CleanUpBufferedSlices();
    return true;
  }
  // Exit if no new data gets acked.
  if (bytes_acked_.Contains(offset, offset + data_length)) {
    return true;
  }
  // Execute the slow path if newly acked data fill in existing holes.
  QuicIntervalSet<QuicStreamOffset> newly_acked(offset, offset + data_length);
  newly_acked.Difference(bytes_acked_);
  for (const auto& interval : newly_acked) {
    *newly_acked_length += (interval.max() - interval.min());
  }
  if (stream_bytes_outstanding_ < *newly_acked_length) {
    return false;
  }
  stream_bytes_outstanding_ -= *newly_acked_length;
  bytes_acked_.Add(offset, offset + data_length);
  pending_retransmissions_.Difference(offset, offset + data_length);
  if (newly_acked.Empty()) {
    return true;
  }
  if (!FreeMemSlices(newly_acked.begin()->min(), newly_acked.rbegin()->max())) {
    return false;
  }
  CleanUpBufferedSlices();
  return true;
}

void QuicStreamSendBuffer::OnStreamDataLost(QuicStreamOffset offset,
                                            QuicByteCount data_length) {
  if (data_length == 0) {
    return;
  }
  QuicIntervalSet<QuicStreamOffset> bytes_lost(offset, offset + data_length);
  bytes_lost.Difference(bytes_acked_);
  if (bytes_lost.Empty()) {
    return;
  }
  for (const auto& lost : bytes_lost) {
    pending_retransmissions_.Add(lost.min(), lost.max());
  }
}

void QuicStreamSendBuffer::OnStreamDataRetransmitted(
    QuicStreamOffset offset, QuicByteCount data_length) {
  if (data_length == 0) {
    return;
  }
  pending_retransmissions_.Difference(offset, offset + data_length);
}

bool QuicStreamSendBuffer::HasPendingRetransmission() const {
  return !pending_retransmissions_.Empty();
}

StreamPendingRetransmission QuicStreamSendBuffer::NextPendingRetransmission()
    const {
  if (HasPendingRetransmission()) {
    const auto pending = pending_retransmissions_.begin();
    return {pending->min(), pending->max() - pending->min()};
  }
  QUIC_BUG(quic_bug_10853_3)
      << "NextPendingRetransmission is called unexpected with no "
         "pending retransmissions.";
  return {0, 0};
}

bool QuicStreamSendBuffer::FreeMemSlices(QuicStreamOffset start,
                                         QuicStreamOffset end) {
  auto it = interval_deque_.DataBegin();
  if (it == interval_deque_.DataEnd() || it->slice.empty()) {
    QUIC_BUG(quic_bug_10853_4)
        << "Trying to ack stream data [" << start << ", " << end << "), "
        << (it == interval_deque_.DataEnd()
                ? "and there is no outstanding data."
                : "and the first slice is empty.");
    return false;
  }
  if (!it->interval().Contains(start)) {
    // Slow path that not the earliest outstanding data gets acked.
    it = std::lower_bound(interval_deque_.DataBegin(),
                          interval_deque_.DataEnd(), start, CompareOffset());
  }
  if (it == interval_deque_.DataEnd() || it->slice.empty()) {
    QUIC_BUG(quic_bug_10853_5)
        << "Offset " << start << " with iterator offset: " << it->offset
        << (it == interval_deque_.DataEnd() ? " does not exist."
                                            : " has already been acked.");
    return false;
  }
  for (; it != interval_deque_.DataEnd(); ++it) {
    if (it->offset >= end) {
      break;
    }
    if (!it->slice.empty() &&
        bytes_acked_.Contains(it->offset, it->offset + it->slice.length())) {
      it->slice.Reset();
    }
  }
  return true;
}

void QuicStreamSendBuffer::CleanUpBufferedSlices() {
  while (!interval_deque_.Empty() &&
         interval_deque_.DataBegin()->slice.empty()) {
    interval_deque_.PopFront();
  }
}

bool QuicStreamSendBuffer::IsStreamDataOutstanding(
    QuicStreamOffset offset, QuicByteCount data_length) const {
  return data_length > 0 &&
         !bytes_acked_.Contains(offset, offset + data_length);
}

size_t QuicStreamSendBuffer::size() const { return interval_deque_.Size(); }

}  // namespace quic
