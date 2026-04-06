// Copyright (c) 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "quiche/quic/core/quic_stream_send_buffer_inlining.h"

#include <algorithm>
#include <cstddef>
#include <utility>

#include "absl/strings/string_view.h"
#include "absl/types/span.h"
#include "quiche/quic/core/quic_data_writer.h"
#include "quiche/quic/core/quic_interval.h"
#include "quiche/quic/core/quic_interval_set.h"
#include "quiche/quic/core/quic_stream_send_buffer_base.h"
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
  bool operator()(const BufferedSliceInlining& slice,
                  QuicStreamOffset offset) const {
    return slice.offset + slice.slice.size() < offset;
  }
};

constexpr bool WillInline(absl::string_view data) {
  return data.size() <= kSendBufferMaxInlinedSize;
}

}  // namespace

BufferedSliceInlining::BufferedSliceInlining(absl::string_view slice,
                                             QuicStreamOffset offset)
    : slice(slice), offset(offset) {}

BufferedSliceInlining::BufferedSliceInlining(BufferedSliceInlining&& other) =
    default;

BufferedSliceInlining& BufferedSliceInlining::operator=(
    BufferedSliceInlining&& other) = default;

BufferedSliceInlining::~BufferedSliceInlining() {}

QuicInterval<uint64_t> BufferedSliceInlining::interval() const {
  const uint64_t length = slice.size();
  return QuicInterval<uint64_t>(offset, offset + length);
}

QuicStreamSendBufferInlining::QuicStreamSendBufferInlining(
    quiche::QuicheBufferAllocator* allocator)
    : allocator_(allocator) {}

void QuicStreamSendBufferInlining::SaveStreamData(absl::string_view data) {
  QUIC_DVLOG(2) << "Save stream data offset " << stream_offset_ << " length "
                << data.length();
  QUICHE_DCHECK(!data.empty());

  if (WillInline(data)) {
    // Skip memory allocation for inlined writes.
    SaveMemSlice(quiche::QuicheMemSlice(
        data.data(), data.size(), +[](absl::string_view) {}));
    return;
  }

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

void QuicStreamSendBufferInlining::SaveMemSlice(quiche::QuicheMemSlice slice) {
  QUIC_DVLOG(2) << "Save slice offset " << stream_offset_ << " length "
                << slice.length();
  if (slice.empty()) {
    QUIC_BUG(quic_bug_10853_1) << "Try to save empty MemSlice to send buffer.";
    return;
  }
  const absl::string_view data = slice.AsStringView();
  const bool is_inlined = WillInline(data);
  interval_deque_.PushBack(BufferedSliceInlining(data, stream_offset_));
  QUICHE_DCHECK_EQ(interval_deque_.DataAt(stream_offset_)->slice.IsInlined(),
                   is_inlined);
  if (!is_inlined) {
    auto [it, success] =
        owned_slices_.emplace(stream_offset_, std::move(slice));
    QUICHE_DCHECK(success);
  }
  stream_offset_ += data.size();
}

QuicByteCount QuicStreamSendBufferInlining::SaveMemSliceSpan(
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

bool QuicStreamSendBufferInlining::WriteStreamData(QuicStreamOffset offset,
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
        slice_it->slice.size() - slice_offset;
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

bool QuicStreamSendBufferInlining::FreeMemSlices(QuicStreamOffset start,
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
        bytes_acked().Contains(it->offset, it->offset + it->slice.size())) {
      ClearSlice(*it);
    }
  }
  return true;
}

void QuicStreamSendBufferInlining::CleanUpBufferedSlices() {
  while (!interval_deque_.Empty() &&
         interval_deque_.DataBegin()->slice.empty()) {
    interval_deque_.PopFront();
  }
}

size_t QuicStreamSendBufferInlining::size() const {
  return interval_deque_.Size();
}

void QuicStreamSendBufferInlining::SetStreamOffsetForTest(
    QuicStreamOffset new_offset) {
  QuicStreamSendBufferBase::SetStreamOffsetForTest(new_offset);
  stream_offset_ = new_offset;
}

absl::string_view QuicStreamSendBufferInlining::LatestWriteForTest() {
  absl::string_view last_slice = "";
  for (auto it = interval_deque_.DataBegin(); it != interval_deque_.DataEnd();
       ++it) {
    last_slice = it->slice.view();
  }
  return last_slice;
}

void QuicStreamSendBufferInlining::ClearSlice(BufferedSliceInlining& slice) {
  if (slice.slice.empty()) {
    return;
  }
  const bool was_inlined = slice.slice.IsInlined();
  slice.slice.clear();
  if (!was_inlined) {
    bool deleted = owned_slices_.erase(slice.offset);
    QUICHE_DCHECK(deleted);
  }
}

QuicByteCount QuicStreamSendBufferInlining::TotalDataBufferedForTest() {
  QuicByteCount length = 0;
  for (auto slice = interval_deque_.DataBegin();
       slice != interval_deque_.DataEnd(); ++slice) {
    length += slice->slice.size();
  }
  return length;
}

}  // namespace quic
