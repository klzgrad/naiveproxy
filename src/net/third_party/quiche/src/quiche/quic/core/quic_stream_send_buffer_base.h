// Copyright (c) 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef QUICHE_QUIC_CORE_QUIC_STREAM_SEND_BUFFER_BASE_H_
#define QUICHE_QUIC_CORE_QUIC_STREAM_SEND_BUFFER_BASE_H_

#include <cstddef>
#include <cstdint>

#include "absl/strings/string_view.h"
#include "absl/types/span.h"
#include "quiche/quic/core/quic_interval_set.h"
#include "quiche/quic/core/quic_types.h"
#include "quiche/common/platform/api/quiche_export.h"
#include "quiche/common/quiche_mem_slice.h"

namespace quic {

namespace test {
class QuicStreamSendBufferPeer;
class QuicStreamPeer;
}  // namespace test

class QuicDataWriter;

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

// Base class for different implementations of QuicStreamSendBuffer.
//
// TODO: b/417402601 - merge those classes back once we are done experimenting
// with different implementations.
class QUICHE_EXPORT QuicStreamSendBufferBase {
 public:
  QuicStreamSendBufferBase() = default;
  QuicStreamSendBufferBase(const QuicStreamSendBufferBase& other) = delete;
  QuicStreamSendBufferBase(QuicStreamSendBufferBase&& other) = delete;
  virtual ~QuicStreamSendBufferBase() = default;

  // Save |data| to send buffer.
  virtual void SaveStreamData(absl::string_view data) = 0;

  // Save |slice| to send buffer.
  virtual void SaveMemSlice(quiche::QuicheMemSlice slice) = 0;

  // Save all slices in |span| to send buffer. Return total bytes saved.
  virtual QuicByteCount SaveMemSliceSpan(
      absl::Span<quiche::QuicheMemSlice> span) = 0;

  // Called when |bytes_consumed| bytes has been consumed by the stream.
  virtual void OnStreamDataConsumed(size_t bytes_consumed);

  // Write |data_length| of data starts at |offset|. Returns true if all data
  // was successfully written. Returns false if the writer fails to write, or if
  // the data was already marked as acked, or if the data was never saved in the
  // first place.
  virtual bool WriteStreamData(QuicStreamOffset offset,
                               QuicByteCount data_length,
                               QuicDataWriter* writer) = 0;

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
  virtual size_t size() const = 0;

  virtual QuicStreamOffset stream_offset() const = 0;

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

  virtual void SetStreamOffsetForTest(QuicStreamOffset new_offset);
  virtual absl::string_view LatestWriteForTest() = 0;
  virtual QuicByteCount TotalDataBufferedForTest() = 0;

 private:
  friend class test::QuicStreamSendBufferPeer;
  friend class test::QuicStreamPeer;

  // Called when data within offset [start, end) gets acked. Frees fully
  // acked buffered slices if any. Returns false if the corresponding data does
  // not exist or has been acked.
  virtual bool FreeMemSlices(QuicStreamOffset start, QuicStreamOffset end) = 0;

  // Cleanup acked data from the start of the interval.
  virtual void CleanUpBufferedSlices() = 0;

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

#endif  // QUICHE_QUIC_CORE_QUIC_STREAM_SEND_BUFFER_BASE_H_
