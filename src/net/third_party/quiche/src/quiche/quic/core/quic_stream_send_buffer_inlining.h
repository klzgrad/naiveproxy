// Copyright (c) 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef QUICHE_QUIC_CORE_QUIC_STREAM_SEND_BUFFER_INLINING_H_
#define QUICHE_QUIC_CORE_QUIC_STREAM_SEND_BUFFER_INLINING_H_

#include <cstddef>
#include <cstdint>

#include "absl/container/flat_hash_map.h"
#include "absl/strings/string_view.h"
#include "absl/types/span.h"
#include "quiche/quic/core/quic_inlined_string_view.h"
#include "quiche/quic/core/quic_interval.h"
#include "quiche/quic/core/quic_interval_deque.h"
#include "quiche/quic/core/quic_interval_set.h"
#include "quiche/quic/core/quic_types.h"
#include "quiche/common/platform/api/quiche_export.h"
#include "quiche/common/quiche_buffer_allocator.h"
#include "quiche/common/quiche_mem_slice.h"

namespace quic {

namespace test {
class QuicStreamSendBufferPeer;
}

class QuicDataWriter;

constexpr size_t kSendBufferMaxInlinedSize = 15;

// BufferedSliceInlining is an entry in the send buffer.  It contains a pointer
// to the buffered data (or data itself, if it is inlined), the size of the data
// and the offset in the buffer.
//
// BufferedSliceInlining does not own contents of the slice; those are freed
// separately. Since we perform a search over an array of BufferedSliceInlining,
// it is important for this data structure to be compact.
struct QUICHE_EXPORT BufferedSliceInlining {
  BufferedSliceInlining(absl::string_view slice, QuicStreamOffset offset);
  BufferedSliceInlining(BufferedSliceInlining&& other);
  BufferedSliceInlining& operator=(BufferedSliceInlining&& other);

  BufferedSliceInlining(const BufferedSliceInlining& other) = delete;
  BufferedSliceInlining& operator=(const BufferedSliceInlining& other) = delete;
  ~BufferedSliceInlining();

  // Return an interval representing the offset and length.
  QuicInterval<uint64_t> interval() const;

  // Stream data of this data slice.
  QuicInlinedStringView<kSendBufferMaxInlinedSize + 1> slice;

  // Location of this data slice in the stream.
  QuicStreamOffset offset;
};

struct QUICHE_EXPORT StreamPendingRetransmission {
  constexpr StreamPendingRetransmission(QuicStreamOffset offset,
                                        QuicByteCount length)
      : offset(offset), length(length) {}

  // Starting offset of this pending retransmission.
  QuicStreamOffset offset;
  // Length of this pending retransmission.
  QuicByteCount length;

  bool operator==(const StreamPendingRetransmission& other) const = default;
};

// QuicStreamSendBuffer contains all of the outstanding (provided by the
// application and not yet acknowledged by the peer) stream data.  Internally it
// is a circular deque of (potentially inlined) QuicheMemSlices, indexed by the
// offset in the stream.  The stream can be accessed randomly in O(log(n)) time,
// though if the offsets are accessed sequentially, the access will be O(1).
class QUICHE_EXPORT QuicStreamSendBufferInlining {
 public:
  explicit QuicStreamSendBufferInlining(
      quiche::QuicheBufferAllocator* allocator);

  // Called when |bytes_consumed| bytes has been consumed by the stream.
  void OnStreamDataConsumed(size_t bytes_consumed);

  // Called when data [offset, offset + data_length) is acked or removed as
  // stream is canceled. Removes fully acked data slice from send buffer. Set
  // |newly_acked_length|. Returns false if trying to ack unsent data.
  bool OnStreamDataAcked(QuicStreamOffset offset, QuicByteCount data_length,
                         QuicByteCount* newly_acked_length);

  // Called when data [offset, offset + data_length) is considered as lost.
  void OnStreamDataLost(QuicStreamOffset offset, QuicByteCount data_length);

  // Called when data [offset, offset + length) was retransmitted.
  void OnStreamDataRetransmitted(QuicStreamOffset offset,
                                 QuicByteCount data_length);

  // Returns true if there is pending retransmissions.
  bool HasPendingRetransmission() const;

  // Returns next pending retransmissions.
  StreamPendingRetransmission NextPendingRetransmission() const;

  // Returns true if data [offset, offset + data_length) is outstanding and
  // waiting to be acked. Returns false otherwise.
  bool IsStreamDataOutstanding(QuicStreamOffset offset,
                               QuicByteCount data_length) const;

  uint64_t stream_bytes_written() const { return stream_bytes_written_; }

  uint64_t stream_bytes_outstanding() const {
    return stream_bytes_outstanding_;
  }

  const QuicIntervalSet<QuicStreamOffset>& bytes_acked() const {
    return bytes_acked_;
  }

  const QuicIntervalSet<QuicStreamOffset>& pending_retransmissions() const {
    return pending_retransmissions_;
  }

  // Save |data| to send buffer.
  void SaveStreamData(absl::string_view data);

  // Save |slice| to send buffer.
  void SaveMemSlice(quiche::QuicheMemSlice slice);

  // Save all slices in |span| to send buffer. Return total bytes saved.
  QuicByteCount SaveMemSliceSpan(absl::Span<quiche::QuicheMemSlice> span);

  // Write |data_length| of data starts at |offset|. Returns true if all data
  // was successfully written. Returns false if the writer fails to write, or if
  // the data was already marked as acked, or if the data was never saved in the
  // first place.
  bool WriteStreamData(QuicStreamOffset offset, QuicByteCount data_length,
                       QuicDataWriter* writer);

  // Number of data slices in send buffer.
  size_t size() const;

  QuicStreamOffset stream_offset() const { return stream_offset_; }

  void SetStreamOffsetForTest(QuicStreamOffset new_offset);
  absl::string_view LatestWriteForTest();
  QuicByteCount TotalDataBufferedForTest();

 private:
  friend class test::QuicStreamSendBufferPeer;

  // Called when data within offset [start, end) gets acked. Frees fully
  // acked buffered slices if any. Returns false if the corresponding data does
  // not exist or has been acked.
  bool FreeMemSlices(QuicStreamOffset start, QuicStreamOffset end);

  // Cleanup acked data from the start of the interval.
  void CleanUpBufferedSlices();

  // Frees an individual buffered slice.
  void ClearSlice(BufferedSliceInlining& slice);

  // Bytes that have been consumed by the stream.
  uint64_t stream_bytes_written_ = 0;

  // Bytes that have been consumed and are waiting to be acked.
  uint64_t stream_bytes_outstanding_ = 0;

  // Offsets of data that has been acked.
  QuicIntervalSet<QuicStreamOffset> bytes_acked_;

  // Data considered as lost and needs to be retransmitted.
  QuicIntervalSet<QuicStreamOffset> pending_retransmissions_;

  // Contains actual stream data.
  QuicIntervalDeque<BufferedSliceInlining> interval_deque_;

  // Offset of next inserted byte.
  QuicStreamOffset stream_offset_ = 0;

  // For slices that are not inlined, contains a map from the offset of the
  // slice in the buffer to the slice release callback.  Those are stored
  // separately from `interval_deque_`, since the callbacks themselves can be
  // quite large, and for many slices, those would not be present.
  absl::flat_hash_map<QuicStreamOffset, quiche::QuicheMemSlice> owned_slices_;

  quiche::QuicheBufferAllocator* allocator_;
};

}  // namespace quic

#endif  // QUICHE_QUIC_CORE_QUIC_STREAM_SEND_BUFFER_INLINING_H_
