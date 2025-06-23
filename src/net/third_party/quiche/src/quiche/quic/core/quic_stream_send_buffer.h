// Copyright (c) 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef QUICHE_QUIC_CORE_QUIC_STREAM_SEND_BUFFER_H_
#define QUICHE_QUIC_CORE_QUIC_STREAM_SEND_BUFFER_H_

#include <cstddef>
#include <cstdint>

#include "absl/strings/string_view.h"
#include "absl/types/span.h"
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
class QuicStreamPeer;
}  // namespace test

class QuicDataWriter;

// BufferedSlice comprises information of a piece of stream data stored in
// contiguous memory space. Please note, BufferedSlice is constructed when
// stream data is saved in send buffer and is removed when stream data is fully
// acked. It is move-only.
struct QUICHE_EXPORT BufferedSlice {
  BufferedSlice(quiche::QuicheMemSlice mem_slice, QuicStreamOffset offset);
  BufferedSlice(BufferedSlice&& other);
  BufferedSlice& operator=(BufferedSlice&& other);

  BufferedSlice(const BufferedSlice& other) = delete;
  BufferedSlice& operator=(const BufferedSlice& other) = delete;
  ~BufferedSlice();

  // Return an interval representing the offset and length.
  QuicInterval<std::size_t> interval() const;

  // Stream data of this data slice.
  quiche::QuicheMemSlice slice;
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

  bool operator==(const StreamPendingRetransmission& other) const;
};

// QuicStreamSendBuffer contains a list of QuicStreamDataSlices. New data slices
// are added to the tail of the list. Data slices are removed from the head of
// the list when they get fully acked. Stream data can be retrieved and acked
// across slice boundaries. Stream data must be saved before being written, and
// it cannot be written after it is marked as acked. Stream data can be written
// out-of-order within those bounds, but note that in-order wites are O(1)
// whereas out-of-order writes are O(log(n)), see QuicIntervalDeque for details.
class QUICHE_EXPORT QuicStreamSendBuffer {
 public:
  explicit QuicStreamSendBuffer(quiche::QuicheBufferAllocator* allocator);
  QuicStreamSendBuffer(const QuicStreamSendBuffer& other) = delete;
  QuicStreamSendBuffer(QuicStreamSendBuffer&& other) = delete;
  ~QuicStreamSendBuffer();

  // Save |data| to send buffer.
  void SaveStreamData(absl::string_view data);

  // Save |slice| to send buffer.
  void SaveMemSlice(quiche::QuicheMemSlice slice);

  // Save all slices in |span| to send buffer. Return total bytes saved.
  QuicByteCount SaveMemSliceSpan(absl::Span<quiche::QuicheMemSlice> span);

  // Called when |bytes_consumed| bytes has been consumed by the stream.
  void OnStreamDataConsumed(size_t bytes_consumed);

  // Write |data_length| of data starts at |offset|. Returns true if all data
  // was successfully written. Returns false if the writer fails to write, or if
  // the data was already marked as acked, or if the data was never saved in the
  // first place.
  bool WriteStreamData(QuicStreamOffset offset, QuicByteCount data_length,
                       QuicDataWriter* writer);

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

  // Number of data slices in send buffer.
  size_t size() const;

  QuicStreamOffset stream_offset() const { return stream_offset_; }

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

 private:
  friend class test::QuicStreamSendBufferPeer;
  friend class test::QuicStreamPeer;

  // Called when data within offset [start, end) gets acked. Frees fully
  // acked buffered slices if any. Returns false if the corresponding data does
  // not exist or has been acked.
  bool FreeMemSlices(QuicStreamOffset start, QuicStreamOffset end);

  // Cleanup acked data from the start of the interval.
  void CleanUpBufferedSlices();

  QuicIntervalDeque<BufferedSlice> interval_deque_;

  // Offset of next inserted byte.
  QuicStreamOffset stream_offset_ = 0;

  quiche::QuicheBufferAllocator* allocator_;

  // Bytes that have been consumed by the stream.
  uint64_t stream_bytes_written_ = 0;

  // Bytes that have been consumed and are waiting to be acked.
  uint64_t stream_bytes_outstanding_ = 0;

  // Offsets of data that has been acked.
  QuicIntervalSet<QuicStreamOffset> bytes_acked_;

  // Data considered as lost and needs to be retransmitted.
  QuicIntervalSet<QuicStreamOffset> pending_retransmissions_;
};

}  // namespace quic

#endif  // QUICHE_QUIC_CORE_QUIC_STREAM_SEND_BUFFER_H_
