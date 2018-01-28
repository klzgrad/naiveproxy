// Copyright (c) 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef NET_QUIC_CORE_QUIC_STREAM_SEND_BUFFER_H_
#define NET_QUIC_CORE_QUIC_STREAM_SEND_BUFFER_H_

#include "net/quic/core/frames/quic_stream_frame.h"
#include "net/quic/core/quic_iovector.h"
#include "net/quic/platform/api/quic_containers.h"
#include "net/quic/platform/api/quic_mem_slice.h"

namespace net {

namespace test {
class QuicStreamSendBufferPeer;
}  // namespace test

class QuicDataWriter;

// BufferedSlice comprises information of a piece of stream data stored in
// contiguous memory space. Please note, BufferedSlice is constructed when
// stream data is saved in send buffer and is removed when stream data is fully
// acked. It is move-only.
struct BufferedSlice {
  BufferedSlice(QuicMemSlice mem_slice, QuicStreamOffset offset);
  BufferedSlice(BufferedSlice&& other);
  BufferedSlice& operator=(BufferedSlice&& other);

  BufferedSlice(const BufferedSlice& other) = delete;
  BufferedSlice& operator=(const BufferedSlice& other) = delete;
  ~BufferedSlice();

  // Stream data of this data slice.
  QuicMemSlice slice;
  // Location of this data slice in the stream.
  QuicStreamOffset offset;
  // Length of payload which is outstanding and waiting for acks.
  QuicByteCount outstanding_data_length;
};

// QuicStreamSendBuffer contains a list of QuicStreamDataSlices. New data slices
// are added to the tail of the list. Data slices are removed from the head of
// the list when they get fully acked. Stream data can be retrieved and acked
// across slice boundaries.
class QUIC_EXPORT_PRIVATE QuicStreamSendBuffer {
 public:
  explicit QuicStreamSendBuffer(QuicBufferAllocator* allocator);
  QuicStreamSendBuffer(const QuicStreamSendBuffer& other) = delete;
  QuicStreamSendBuffer(QuicStreamSendBuffer&& other) = delete;
  ~QuicStreamSendBuffer();

  // Save |data_length| of data starts at |iov_offset| in |iov| to send buffer.
  void SaveStreamData(QuicIOVector iov,
                      size_t iov_offset,
                      QuicByteCount data_length);

  // Save |slice| to send buffer.
  void SaveMemSlice(QuicMemSlice slice);

  // Write |data_length| of data starts at |offset|.
  bool WriteStreamData(QuicStreamOffset offset,
                       QuicByteCount data_length,
                       QuicDataWriter* writer);

  // Called when data [offset, offset + data_length) is acked or removed as
  // stream is canceled. Removes fully acked data slice from send buffer.
  void RemoveStreamFrame(QuicStreamOffset offset, QuicByteCount data_length);

  // Number of data slices in send buffer.
  size_t size() const;

  QuicStreamOffset stream_offset() const { return stream_offset_; }

 private:
  friend class test::QuicStreamSendBufferPeer;

  QuicDeque<BufferedSlice> buffered_slices_;

  // Offset of next inserted byte.
  QuicStreamOffset stream_offset_;

  QuicBufferAllocator* allocator_;
};

}  // namespace net

#endif  // NET_QUIC_CORE_QUIC_STREAM_SEND_BUFFER_H_
