// Copyright (c) 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef NET_THIRD_PARTY_QUIC_QUARTC_QUARTC_STREAM_H_
#define NET_THIRD_PARTY_QUIC_QUARTC_QUARTC_STREAM_H_

#include "net/third_party/quic/core/quic_session.h"
#include "net/third_party/quic/core/quic_stream.h"
#include "net/third_party/quic/platform/api/quic_export.h"
#include "net/third_party/quic/platform/api/quic_mem_slice_span.h"

namespace quic {

// Sends and receives data with a particular QUIC stream ID, reliably and
// in-order. To send/receive data out of order, use separate streams. To
// send/receive unreliably, close a stream after reliability is no longer
// needed.
class QUIC_EXPORT_PRIVATE QuartcStream : public QuicStream {
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
  // setting |max_frame_retransmission_count| to zero.
  bool cancel_on_loss();
  void set_cancel_on_loss(bool cancel_on_loss);

  // Maximum number of stream frames which may be retransmitted.  Up to this
  // number of stream frames may be retransmitted.  If any stream frames in
  // excess of this amount would be retransmitted, the stream will reset itself.
  // Setting it to zero disables retransmissions.
  //
  // Ideally, the stream would support a maximum retransmission count per frame,
  // allowing each frame to be retransmitted up to N times.  However, this
  // requires complex bookkeeping (tracking retransmission count on a per-frame
  // basis).  This feature provides a simple way to limit retransmissions on
  // streams that are expected to fit within one frame (eg. small messages).
  int max_frame_retransmission_count() const;
  void set_max_frame_retransmission_count(int max_frame_retransmission_count);

  QuicByteCount BytesPendingRetransmission();

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

  // Maximum number of frames which may be retransmitted on this stream.
  int max_frame_retransmission_count_ = std::numeric_limits<int>::max();

  // Total number of stream frames detected as lost.
  int total_frames_lost_ = 0;
};

}  // namespace quic

#endif  // NET_THIRD_PARTY_QUIC_QUARTC_QUARTC_STREAM_H_
