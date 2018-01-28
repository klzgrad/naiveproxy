// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef NET_QUIC_CORE_QUIC_STREAM_SEQUENCER_H_
#define NET_QUIC_CORE_QUIC_STREAM_SEQUENCER_H_

#include <cstddef>
#include <map>

#include "base/macros.h"
#include "net/quic/core/quic_packets.h"
#include "net/quic/core/quic_stream_sequencer_buffer.h"
#include "net/quic/platform/api/quic_export.h"

namespace net {

namespace test {
class QuicStreamSequencerPeer;
}  // namespace test

class QuicClock;
class QuicStream;

// Buffers frames until we have something which can be passed
// up to the next layer.
class QUIC_EXPORT_PRIVATE QuicStreamSequencer {
 public:
  QuicStreamSequencer(QuicStream* quic_stream, const QuicClock* clock);
  virtual ~QuicStreamSequencer();

  // If the frame is the next one we need in order to process in-order data,
  // ProcessData will be immediately called on the stream until all buffered
  // data is processed or the stream fails to consume data.  Any unconsumed
  // data will be buffered. If the frame is not the next in line, it will be
  // buffered.
  void OnStreamFrame(const QuicStreamFrame& frame);

  // Once data is buffered, it's up to the stream to read it when the stream
  // can handle more data.  The following three functions make that possible.

  // Fills in up to iov_len iovecs with the next readable regions.  Returns the
  // number of iovs used.  Non-destructive of the underlying data.
  int GetReadableRegions(iovec* iov, size_t iov_len) const;

  // Fills in one iovec with the next readable region.  |timestamp| is
  // data arrived at the sequencer, and is used for measuring head of
  // line blocking (HOL).  Returns false if there is no readable
  // region available.
  bool GetReadableRegion(iovec* iov, QuicTime* timestamp) const;

  // Copies the data into the iov_len buffers provided.  Returns the number of
  // bytes read.  Any buffered data no longer in use will be released.
  // TODO(rch): remove this method and instead implement it as a helper method
  // based on GetReadableRegions and MarkConsumed.
  int Readv(const struct iovec* iov, size_t iov_len);

  // Consumes |num_bytes| data.  Used in conjunction with |GetReadableRegions|
  // to do zero-copy reads.
  void MarkConsumed(size_t num_bytes);

  // Returns true if the sequncer has bytes available for reading.
  bool HasBytesToRead() const;

  // Returns true if the sequencer has delivered the fin.
  bool IsClosed() const;

  // Calls |OnDataAvailable| on |stream_| if there is buffered data that can
  // be processed, and causes |OnDataAvailable| to be called as new data
  // arrives.
  void SetUnblocked();

  // Blocks processing of frames until |SetUnblocked| is called.
  void SetBlockedUntilFlush();

  // Sets the sequencer to discard all incoming data itself and not call
  // |stream_->OnDataAvailable()|.  |stream_->OnFinRead()| will be called
  // automatically when the FIN is consumed (which may be immediately).
  void StopReading();

  // Free the memory of underlying buffer.
  void ReleaseBuffer();

  // Free the memory of underlying buffer when no bytes remain in it.
  void ReleaseBufferIfEmpty();

  // Number of bytes in the buffer right now.
  size_t NumBytesBuffered() const;

  // Number of bytes has been consumed.
  QuicStreamOffset NumBytesConsumed() const;

  int num_frames_received() const { return num_frames_received_; }

  int num_duplicate_frames_received() const {
    return num_duplicate_frames_received_;
  }

  bool ignore_read_data() const { return ignore_read_data_; }

  // Returns std::string describing internal state.
  const std::string DebugString() const;

 private:
  friend class test::QuicStreamSequencerPeer;

  // Deletes and records as consumed any buffered data that is now in-sequence.
  // (To be called only after StopReading has been called.)
  void FlushBufferedFrames();

  // Wait until we've seen 'offset' bytes, and then terminate the stream.
  void CloseStreamAtOffset(QuicStreamOffset offset);

  // If we've received a FIN and have processed all remaining data, then inform
  // the stream of FIN, and clear buffers.
  bool MaybeCloseStream();

  // The stream which owns this sequencer.
  QuicStream* stream_;

  // Stores received data in offset order.
  QuicStreamSequencerBuffer buffered_frames_;

  // The offset, if any, we got a stream termination for.  When this many bytes
  // have been processed, the sequencer will be closed.
  QuicStreamOffset close_offset_;

  // If true, the sequencer is blocked from passing data to the stream and will
  // buffer all new incoming data until FlushBufferedFrames is called.
  bool blocked_;

  // Count of the number of frames received.
  int num_frames_received_;

  // Count of the number of duplicate frames received.
  int num_duplicate_frames_received_;

  // Not owned.
  const QuicClock* clock_;

  // If true, all incoming data will be discarded.
  bool ignore_read_data_;

  DISALLOW_COPY_AND_ASSIGN(QuicStreamSequencer);
};

}  // namespace net

#endif  // NET_QUIC_CORE_QUIC_STREAM_SEQUENCER_H_
