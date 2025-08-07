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
#include "quiche/quic/core/quic_stream_send_buffer_base.h"
#include "quiche/quic/core/quic_types.h"
#include "quiche/common/platform/api/quiche_export.h"
#include "quiche/common/quiche_buffer_allocator.h"
#include "quiche/common/quiche_mem_slice.h"

namespace quic {

namespace test {
class QuicStreamSendBufferPeer;
}

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

// QuicStreamSendBuffer contains a list of QuicStreamDataSlices. New data slices
// are added to the tail of the list. Data slices are removed from the head of
// the list when they get fully acked. Stream data can be retrieved and acked
// across slice boundaries. Stream data must be saved before being written, and
// it cannot be written after it is marked as acked. Stream data can be written
// out-of-order within those bounds, but note that in-order wites are O(1)
// whereas out-of-order writes are O(log(n)), see QuicIntervalDeque for details.
class QUICHE_EXPORT QuicStreamSendBuffer : public QuicStreamSendBufferBase {
 public:
  explicit QuicStreamSendBuffer(quiche::QuicheBufferAllocator* allocator);

  // Save |data| to send buffer.
  void SaveStreamData(absl::string_view data) override;

  // Save |slice| to send buffer.
  void SaveMemSlice(quiche::QuicheMemSlice slice) override;

  // Save all slices in |span| to send buffer. Return total bytes saved.
  QuicByteCount SaveMemSliceSpan(
      absl::Span<quiche::QuicheMemSlice> span) override;

  // Write |data_length| of data starts at |offset|. Returns true if all data
  // was successfully written. Returns false if the writer fails to write, or if
  // the data was already marked as acked, or if the data was never saved in the
  // first place.
  bool WriteStreamData(QuicStreamOffset offset, QuicByteCount data_length,
                       QuicDataWriter* writer) override;

  // Number of data slices in send buffer.
  size_t size() const override;

  QuicStreamOffset stream_offset() const override { return stream_offset_; }

  void SetStreamOffsetForTest(QuicStreamOffset new_offset) override;
  absl::string_view LatestWriteForTest() override;
  QuicByteCount TotalDataBufferedForTest() override;

 private:
  friend class test::QuicStreamSendBufferPeer;

  // Called when data within offset [start, end) gets acked. Frees fully
  // acked buffered slices if any. Returns false if the corresponding data does
  // not exist or has been acked.
  bool FreeMemSlices(QuicStreamOffset start, QuicStreamOffset end) override;

  // Cleanup acked data from the start of the interval.
  void CleanUpBufferedSlices() override;

  QuicIntervalDeque<BufferedSlice> interval_deque_;

  // Offset of next inserted byte.
  QuicStreamOffset stream_offset_ = 0;

  quiche::QuicheBufferAllocator* allocator_;
};

}  // namespace quic

#endif  // QUICHE_QUIC_CORE_QUIC_STREAM_SEND_BUFFER_H_
