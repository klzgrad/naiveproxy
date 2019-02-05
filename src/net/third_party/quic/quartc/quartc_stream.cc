// Copyright (c) 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/third_party/quic/quartc/quartc_stream.h"

#include "net/third_party/quic/platform/api/quic_string_piece.h"

namespace quic {

QuartcStream::QuartcStream(QuicStreamId id, QuicSession* session)
    : QuicStream(id, session, /*is_static=*/false, BIDIRECTIONAL) {
  sequencer()->set_level_triggered(true);
}

QuartcStream::~QuartcStream() {}

void QuartcStream::OnDataAvailable() {
  bool fin = sequencer()->ReadableBytes() + sequencer()->NumBytesConsumed() ==
             sequencer()->close_offset();

  // Upper bound on number of readable regions.  Each complete block's worth of
  // data crosses at most one region boundary.  The remainder may cross one more
  // boundary.  Number of regions is one more than the number of region
  // boundaries crossed.
  size_t iov_length = sequencer()->ReadableBytes() /
                          QuicStreamSequencerBuffer::kBlockSizeBytes +
                      2;
  std::unique_ptr<iovec[]> iovecs = QuicMakeUnique<iovec[]>(iov_length);
  iov_length = sequencer()->GetReadableRegions(iovecs.get(), iov_length);

  sequencer()->MarkConsumed(
      delegate_->OnReceived(this, iovecs.get(), iov_length, fin));
  if (sequencer()->IsClosed()) {
    OnFinRead();
  }
}

void QuartcStream::OnClose() {
  QuicStream::OnClose();
  DCHECK(delegate_);
  delegate_->OnClose(this);
}

void QuartcStream::OnStreamDataConsumed(size_t bytes_consumed) {
  QuicStream::OnStreamDataConsumed(bytes_consumed);

  DCHECK(delegate_);
  delegate_->OnBufferChanged(this);
}

void QuartcStream::OnDataBuffered(
    QuicStreamOffset offset,
    QuicByteCount data_length,
    const QuicReferenceCountedPointer<QuicAckListenerInterface>& ack_listener) {
  DCHECK(delegate_);
  delegate_->OnBufferChanged(this);
}

void QuartcStream::OnStreamFrameRetransmitted(QuicStreamOffset offset,
                                              QuicByteCount data_length,
                                              bool fin_retransmitted) {
  QuicStream::OnStreamFrameRetransmitted(offset, data_length,
                                         fin_retransmitted);

  DCHECK(delegate_);
  delegate_->OnBufferChanged(this);
}

void QuartcStream::OnStreamFrameLost(QuicStreamOffset offset,
                                     QuicByteCount data_length,
                                     bool fin_lost) {
  QuicStream::OnStreamFrameLost(offset, data_length, fin_lost);

  ++total_frames_lost_;

  DCHECK(delegate_);
  delegate_->OnBufferChanged(this);
}

void QuartcStream::OnCanWrite() {
  if (total_frames_lost_ > max_frame_retransmission_count_ &&
      HasPendingRetransmission()) {
    Reset(QUIC_STREAM_CANCELLED);
    return;
  }
  QuicStream::OnCanWrite();
}

bool QuartcStream::cancel_on_loss() {
  return max_frame_retransmission_count_ == 0;
}

void QuartcStream::set_cancel_on_loss(bool cancel_on_loss) {
  if (cancel_on_loss) {
    max_frame_retransmission_count_ = 0;
  } else {
    max_frame_retransmission_count_ = std::numeric_limits<int>::max();
  }
}

int QuartcStream::max_frame_retransmission_count() const {
  return max_frame_retransmission_count_;
}

void QuartcStream::set_max_frame_retransmission_count(
    int max_frame_retransmission_count) {
  max_frame_retransmission_count_ = max_frame_retransmission_count;
}

QuicByteCount QuartcStream::BytesPendingRetransmission() {
  if (total_frames_lost_ > max_frame_retransmission_count_) {
    return 0;  // Lost bytes will never be retransmitted.
  }
  QuicByteCount bytes = 0;
  for (const auto& interval : send_buffer().pending_retransmissions()) {
    bytes += interval.Length();
  }
  return bytes;
}

void QuartcStream::FinishWriting() {
  WriteOrBufferData(QuicStringPiece(nullptr, 0), true, nullptr);
}

void QuartcStream::SetDelegate(Delegate* delegate) {
  if (delegate_) {
    LOG(WARNING) << "The delegate for Stream " << id()
                 << " has already been set.";
  }
  delegate_ = delegate;
  DCHECK(delegate_);
}

}  // namespace quic
