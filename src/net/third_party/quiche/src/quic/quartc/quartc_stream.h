// Copyright (c) 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef QUICHE_QUIC_QUARTC_QUARTC_STREAM_H_
#define QUICHE_QUIC_QUARTC_QUARTC_STREAM_H_

#include <stddef.h>
#include <limits>

#include "net/third_party/quiche/src/quic/core/quic_ack_listener_interface.h"
#include "net/third_party/quiche/src/quic/core/quic_session.h"
#include "net/third_party/quiche/src/quic/core/quic_stream.h"
#include "net/third_party/quiche/src/quic/core/quic_types.h"
#include "net/third_party/quiche/src/quic/platform/api/quic_mem_slice_span.h"
#include "net/third_party/quiche/src/quic/platform/api/quic_reference_counted.h"
#include "net/quic/platform/impl/quic_export_impl.h"
#include "net/third_party/quiche/src/quic/quartc/quartc_interval_counter.h"

namespace quic {

// Sends and receives data with a particular QUIC stream ID, reliably and
// in-order. To send/receive data out of order, use separate streams. To
// send/receive unreliably, close a stream after reliability is no longer
// needed.
class QuartcStream : public QuicStream {
 public:
  QuartcStream(QuicStreamId id, QuicSession* session);

  ~QuartcStream() override;

  // QuicStream overrides.
  void OnDataAvailable() override;

  void OnClose() override;

  void OnStreamDataConsumed(size_t bytes_consumed) override;

  void OnDataBuffered(
      QuicStreamOffset offset,
      QuicByteCount data_length,
      const QuicReferenceCountedPointer<QuicAckListenerInterface>& ack_listener)
      override;

  bool OnStreamFrameAcked(QuicStreamOffset offset,
                          QuicByteCount data_length,
                          bool fin_acked,
                          QuicTime::Delta ack_delay_time,
                          QuicTime receive_timestamp,
                          QuicByteCount* newly_acked_length) override;

  void OnStreamFrameRetransmitted(QuicStreamOffset offset,
                                  QuicByteCount data_length,
                                  bool fin_retransmitted) override;

  void OnStreamFrameLost(QuicStreamOffset offset,
                         QuicByteCount data_length,
                         bool fin_lost) override;

  void OnCanWrite() override;

  // QuartcStream interface methods.

  // Whether the stream should be cancelled instead of retransmitted on loss.
  // If set to true, the stream will reset itself instead of retransmitting lost
  // stream frames.  Defaults to false.  Setting it to true is equivalent to
  // setting |max_retransmission_count| to zero.
  bool cancel_on_loss();
  void set_cancel_on_loss(bool cancel_on_loss);

  // Maximum number of times this stream's data may be retransmitted.  Each byte
  // of stream data may be retransmitted this many times.  If any byte (or range
  // of bytes) is lost and would be retransmitted more than this number of
  // times, the stream resets itself instead of retransmitting the data again.
  // Setting this value to zero disables retransmissions.
  //
  // Note that this limit applies only to stream data, not to the FIN bit.  If
  // only the FIN bit needs to be retransmitted, there is no benefit to
  // cancelling the stream and sending a reset frame instead.
  int max_retransmission_count() const;
  void set_max_retransmission_count(int max_retransmission_count);

  QuicByteCount BytesPendingRetransmission();

  // Returns the current read offset for this stream.  During a call to
  // Delegate::OnReceived, this value is the offset of the first byte read.
  QuicStreamOffset ReadOffset();

  // Marks this stream as finished writing.  Asynchronously sends a FIN and
  // closes the write-side.  It is not necessary to call FinishWriting() if the
  // last call to Write() sends a FIN.
  void FinishWriting();

  // Implemented by the user of the QuartcStream to receive incoming
  // data and be notified of state changes.
  class Delegate {
   public:
    virtual ~Delegate() {}

    // Called when the stream receives data. |iov| is a pointer to the first of
    // |iov_length| readable regions. |iov| points to readable data within
    // |stream|'s sequencer buffer. QUIC may modify or delete this data after
    // the application consumes it. |fin| indicates the end of stream data.
    // Returns the number of bytes consumed. May return 0 if the delegate is
    // unable to consume any bytes at this time.
    virtual size_t OnReceived(QuartcStream* stream,
                              iovec* iov,
                              size_t iov_length,
                              bool fin) = 0;

    // Called when the stream is closed, either locally or by the remote
    // endpoint.  Streams close when (a) fin bits are both sent and received,
    // (b) Close() is called, or (c) the stream is reset.
    // TODO(zhihuang) Creates a map from the integer error_code to WebRTC native
    // error code.
    virtual void OnClose(QuartcStream* stream) = 0;

    // Called when the contents of the stream's buffer changes.
    virtual void OnBufferChanged(QuartcStream* stream) = 0;
  };

  // The |delegate| is not owned by QuartcStream.
  void SetDelegate(Delegate* delegate);

 private:
  Delegate* delegate_ = nullptr;

  // Maximum number of times this stream's data may be retransmitted.
  int max_retransmission_count_ = std::numeric_limits<int>::max();

  // Counter which tracks the number of times each frame has been lost
  // (accounting for the possibility of overlapping frames).
  //
  // If the maximum count of any lost frame exceeds |max_retransmission_count_|,
  // the stream will cancel itself on the next attempt to retransmit data (the
  // next call to |OnCanWrite|).
  QuartcIntervalCounter<QuicStreamOffset> lost_frame_counter_;
};

}  // namespace quic

#endif  // QUICHE_QUIC_QUARTC_QUARTC_STREAM_H_
