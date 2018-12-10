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

  void OnCanWrite() override;

  // QuartcStream interface methods.

  // Whether the stream should be cancelled instead of retransmitted on loss.
  // If set to true, the stream will reset itself instead of retransmitting lost
  // stream frames.  Defaults to false.
  bool cancel_on_loss();
  void set_cancel_on_loss(bool cancel_on_loss);

  // Marks this stream as finished writing.  Asynchronously sends a FIN and
  // closes the write-side.  It is not necessary to call FinishWriting() if the
  // last call to Write() sends a FIN.
  void FinishWriting();

  // Implemented by the user of the QuartcStream to receive incoming
  // data and be notified of state changes.
  class Delegate {
   public:
    virtual ~Delegate() {}

    // Called when the stream receives data.  Called with |size| == 0 after all
    // stream data has been delivered (once the stream receives a FIN bit).
    // Note that the same packet may include both data and a FIN bit, causing
    // this method to be called twice.
    virtual void OnReceived(QuartcStream* stream,
                            const char* data,
                            size_t size) = 0;

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

  // Whether the stream should cancel itself instead of retransmitting frames.
  bool cancel_on_loss_ = false;
};

}  // namespace quic

#endif  // NET_THIRD_PARTY_QUIC_QUARTC_QUARTC_STREAM_H_
