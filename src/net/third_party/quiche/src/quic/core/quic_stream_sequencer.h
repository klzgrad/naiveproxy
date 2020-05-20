// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef QUICHE_QUIC_CORE_QUIC_STREAM_SEQUENCER_H_
#define QUICHE_QUIC_CORE_QUIC_STREAM_SEQUENCER_H_

#include <cstddef>
#include <map>
#include <string>

#include "net/third_party/quiche/src/quic/core/quic_packets.h"
#include "net/third_party/quiche/src/quic/core/quic_stream_sequencer_buffer.h"
#include "net/third_party/quiche/src/quic/core/quic_types.h"
#include "net/third_party/quiche/src/quic/platform/api/quic_export.h"

namespace quic {

namespace test {
class QuicStreamSequencerPeer;
}  // namespace test

// Buffers frames until we have something which can be passed
// up to the next layer.
class QUIC_EXPORT_PRIVATE QuicStreamSequencer {
 public:
  // Interface that thie Sequencer uses to communicate with the Stream.
  class QUIC_EXPORT_PRIVATE StreamInterface {
   public:
    virtual ~StreamInterface() = default;

    // Called when new data is available to be read from the sequencer.
    virtual void OnDataAvailable() = 0;
    // Called when the end of the stream has been read.
    virtual void OnFinRead() = 0;
    // Called when bytes have been consumed from the sequencer.
    virtual void AddBytesConsumed(QuicByteCount bytes) = 0;
    // Called when an error has occurred which should result in the stream
    // being reset.
    virtual void Reset(QuicRstStreamErrorCode error) = 0;
    // Called when an error has occurred which should result in the connection
    // being closed.
    virtual void OnUnrecoverableError(QuicErrorCode error,
                                      const std::string& details) = 0;
    // Returns the stream id of this stream.
    virtual QuicStreamId id() const = 0;
  };

  explicit QuicStreamSequencer(StreamInterface* quic_stream);
  QuicStreamSequencer(const QuicStreamSequencer&) = delete;
  QuicStreamSequencer(QuicStreamSequencer&&) = default;
  QuicStreamSequencer& operator=(const QuicStreamSequencer&) = delete;
  virtual ~QuicStreamSequencer();

  // If the frame is the next one we need in order to process in-order data,
  // ProcessData will be immediately called on the stream until all buffered
  // data is processed or the stream fails to consume data.  Any unconsumed
  // data will be buffered. If the frame is not the next in line, it will be
  // buffered.
  void OnStreamFrame(const QuicStreamFrame& frame);

  // If the frame is the next one we need in order to process in-order data,
  // ProcessData will be immediately called on the crypto stream until all
  // buffered data is processed or the crypto stream fails to consume data. Any
  // unconsumed data will be buffered. If the frame is not the next in line, it
  // will be buffered.
  void OnCryptoFrame(const QuicCryptoFrame& frame);

  // Once data is buffered, it's up to the stream to read it when the stream
  // can handle more data.  The following three functions make that possible.

  // Fills in up to iov_len iovecs with the next readable regions.  Returns the
  // number of iovs used.  Non-destructive of the underlying data.
  int GetReadableRegions(iovec* iov, size_t iov_len) const;

  // Fills in one iovec with the next readable region.  Returns false if there
  // is no readable region available.
  bool GetReadableRegion(iovec* iov) const;

  // Fills in one iovec with the region starting at |offset| and returns true.
  // Returns false if no readable region is available, either because data has
  // not been received yet or has already been consumed.
  bool PeekRegion(QuicStreamOffset offset, iovec* iov) const;

  // Copies the data into the iov_len buffers provided.  Returns the number of
  // bytes read.  Any buffered data no longer in use will be released.
  // TODO(rch): remove this method and instead implement it as a helper method
  // based on GetReadableRegions and MarkConsumed.
  size_t Readv(const struct iovec* iov, size_t iov_len);

  // Consumes |num_bytes| data.  Used in conjunction with |GetReadableRegions|
  // to do zero-copy reads.
  void MarkConsumed(size_t num_bytes);

  // Appends all of the readable data to |buffer| and marks all of the appended
  // data as consumed.
  void Read(std::string* buffer);

  // Returns true if the sequncer has bytes available for reading.
  bool HasBytesToRead() const;

  // Number of bytes available to read.
  size_t ReadableBytes() const;

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

  QuicStreamOffset close_offset() const { return close_offset_; }

  int num_frames_received() const { return num_frames_received_; }

  int num_duplicate_frames_received() const {
    return num_duplicate_frames_received_;
  }

  bool ignore_read_data() const { return ignore_read_data_; }

  void set_level_triggered(bool level_triggered) {
    level_triggered_ = level_triggered;
  }

  bool level_triggered() const { return level_triggered_; }

  void set_stream(StreamInterface* stream) { stream_ = stream; }

  // Returns string describing internal state.
  const std::string DebugString() const;

 private:
  friend class test::QuicStreamSequencerPeer;

  // Deletes and records as consumed any buffered data that is now in-sequence.
  // (To be called only after StopReading has been called.)
  void FlushBufferedFrames();

  // Wait until we've seen 'offset' bytes, and then terminate the stream.
  // Returns true if |stream_| is still available to receive data, and false if
  // |stream_| is reset.
  bool CloseStreamAtOffset(QuicStreamOffset offset);

  // If we've received a FIN and have processed all remaining data, then inform
  // the stream of FIN, and clear buffers.
  void MaybeCloseStream();

  // Shared implementation between OnStreamFrame and OnCryptoFrame.
  void OnFrameData(QuicStreamOffset byte_offset,
                   size_t data_len,
                   const char* data_buffer);

  // The stream which owns this sequencer.
  StreamInterface* stream_;

  // Stores received data in offset order.
  QuicStreamSequencerBuffer buffered_frames_;

  // The highest offset that is received so far.
  QuicStreamOffset highest_offset_;

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

  // If true, all incoming data will be discarded.
  bool ignore_read_data_;

  // If false, only call OnDataAvailable() when it becomes newly unblocked.
  // Otherwise, call OnDataAvailable() when number of readable bytes changes.
  bool level_triggered_;
};

}  // namespace quic

#endif  // QUICHE_QUIC_CORE_QUIC_STREAM_SEQUENCER_H_
