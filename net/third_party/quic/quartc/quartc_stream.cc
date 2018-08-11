// Copyright (c) 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/third_party/quic/quartc/quartc_stream.h"

#include "net/third_party/quic/platform/api/quic_string_piece.h"

namespace net {

QuartcStream::QuartcStream(QuicStreamId id, QuicSession* session)
    : QuicStream(id, session, /*is_static=*/false) {}
QuartcStream::~QuartcStream() {}

void QuartcStream::OnDataAvailable() {
  struct iovec iov;
  while (sequencer()->GetReadableRegion(&iov)) {
    DCHECK(delegate_);
    delegate_->OnReceived(this, reinterpret_cast<const char*>(iov.iov_base),
                          iov.iov_len);
    sequencer()->MarkConsumed(iov.iov_len);
  }
  // All the data has been received if the sequencer is closed.
  // Notify the delegate by calling the callback function one more time with
  // iov_len = 0.
  if (sequencer()->IsClosed()) {
    OnFinRead();
    delegate_->OnReceived(this, reinterpret_cast<const char*>(iov.iov_base), 0);
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

uint32_t QuartcStream::stream_id() {
  return id();
}

uint64_t QuartcStream::bytes_buffered() {
  return BufferedDataBytes();
}

bool QuartcStream::fin_sent() {
  return QuicStream::fin_sent();
}

int QuartcStream::stream_error() {
  return QuicStream::stream_error();
}

void QuartcStream::Write(QuicMemSliceSpan data, const WriteParameters& param) {
  WriteMemSlices(data, param.fin);
}

void QuartcStream::FinishWriting() {
  WriteOrBufferData(QuicStringPiece(nullptr, 0), true, nullptr);
}

void QuartcStream::FinishReading() {
  QuicStream::StopReading();
}

void QuartcStream::Close() {
  QuicStream::session()->CloseStream(id());
}

void QuartcStream::SetDelegate(QuartcStreamInterface::Delegate* delegate) {
  if (delegate_) {
    LOG(WARNING) << "The delegate for Stream " << id()
                 << " has already been set.";
  }
  delegate_ = delegate;
  DCHECK(delegate_);
}

}  // namespace net
