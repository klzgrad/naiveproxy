// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/quic/core/quic_headers_stream.h"

#include "net/quic/core/quic_spdy_session.h"
#include "net/quic/platform/api/quic_flags.h"

namespace net {

QuicHeadersStream::CompressedHeaderInfo::CompressedHeaderInfo(
    QuicStreamOffset headers_stream_offset,
    QuicStreamOffset full_length,
    QuicReferenceCountedPointer<QuicAckListenerInterface> ack_listener)
    : headers_stream_offset(headers_stream_offset),
      full_length(full_length),
      unacked_length(full_length),
      ack_listener(std::move(ack_listener)) {}

QuicHeadersStream::CompressedHeaderInfo::CompressedHeaderInfo(
    const CompressedHeaderInfo& other) = default;

QuicHeadersStream::CompressedHeaderInfo::~CompressedHeaderInfo() {}

QuicHeadersStream::QuicHeadersStream(QuicSpdySession* session)
    : QuicStream(kHeadersStreamId, session), spdy_session_(session) {
  // The headers stream is exempt from connection level flow control.
  DisableConnectionFlowControlForThisStream();
}

QuicHeadersStream::~QuicHeadersStream() {}

void QuicHeadersStream::OnDataAvailable() {
  char buffer[1024];
  struct iovec iov;
  QuicTime timestamp(QuicTime::Zero());
  while (true) {
    iov.iov_base = buffer;
    iov.iov_len = arraysize(buffer);
    if (!sequencer()->GetReadableRegion(&iov, &timestamp)) {
      // No more data to read.
      break;
    }
    if (spdy_session_->ProcessHeaderData(iov, timestamp) != iov.iov_len) {
      // Error processing data.
      return;
    }
    sequencer()->MarkConsumed(iov.iov_len);
    MaybeReleaseSequencerBuffer();
  }
}

void QuicHeadersStream::MaybeReleaseSequencerBuffer() {
  if (spdy_session_->ShouldReleaseHeadersStreamSequencerBuffer()) {
    sequencer()->ReleaseBufferIfEmpty();
  }
}

void QuicHeadersStream::OnStreamFrameAcked(const QuicStreamFrame& frame,
                                           QuicTime::Delta ack_delay_time) {
  QuicStreamOffset offset = frame.offset;
  QuicByteCount length = frame.data_length;
  for (CompressedHeaderInfo& header : unacked_headers_) {
    if (offset < header.headers_stream_offset) {
      // This header frame offset belongs to headers with smaller offset, stop
      // processing.
      break;
    }

    if (offset >= header.headers_stream_offset + header.full_length) {
      // This header frame belongs to headers with larger offset.
      continue;
    }

    QuicByteCount header_offset = offset - header.headers_stream_offset;
    QuicByteCount acked_length =
        std::min(length, header.full_length - header_offset);

    if (header.unacked_length < acked_length) {
      QUIC_BUG << "Unsent stream data is acked. unacked_length: "
               << header.unacked_length << " acked_length: " << acked_length;
      CloseConnectionWithDetails(QUIC_INTERNAL_ERROR,
                                 "Unsent stream data is acked");
      return;
    }
    if (header.ack_listener != nullptr && acked_length > 0) {
      header.ack_listener->OnPacketAcked(acked_length, ack_delay_time);
    }
    header.unacked_length -= acked_length;
    offset += acked_length;
    length -= acked_length;
  }

  // Remove headers which are fully acked. Please note, header frames can be
  // acked out of order, but unacked_headers_ is cleaned up in order.
  while (!unacked_headers_.empty() &&
         unacked_headers_.front().unacked_length == 0) {
    unacked_headers_.pop_front();
  }
  QuicStream::OnStreamFrameAcked(frame, ack_delay_time);
}

void QuicHeadersStream::OnStreamFrameRetransmitted(
    const QuicStreamFrame& frame) {
  QuicStreamOffset offset = frame.offset;
  QuicByteCount length = frame.data_length;
  for (CompressedHeaderInfo& header : unacked_headers_) {
    if (offset < header.headers_stream_offset) {
      // This header frame offset belongs to headers with smaller offset, stop
      // processing.
      break;
    }

    if (offset >= header.headers_stream_offset + header.full_length) {
      // This header frame belongs to headers with larger offset.
      continue;
    }

    QuicByteCount header_offset = offset - header.headers_stream_offset;
    QuicByteCount retransmitted_length =
        std::min(length, header.full_length - header_offset);
    if (header.ack_listener != nullptr && retransmitted_length > 0) {
      header.ack_listener->OnPacketRetransmitted(retransmitted_length);
    }
    offset += retransmitted_length;
    length -= retransmitted_length;
  }
}

void QuicHeadersStream::OnDataBuffered(
    QuicStreamOffset offset,
    QuicByteCount data_length,
    const QuicReferenceCountedPointer<QuicAckListenerInterface>& ack_listener) {
  // Populate unacked_headers_.
  if (!unacked_headers_.empty() &&
      (offset == unacked_headers_.back().headers_stream_offset +
                     unacked_headers_.back().full_length) &&
      ack_listener == unacked_headers_.back().ack_listener) {
    // Try to combine with latest inserted entry if they belong to the same
    // header (i.e., having contiguous offset and the same ack listener).
    unacked_headers_.back().full_length += data_length;
    unacked_headers_.back().unacked_length += data_length;
  } else {
    unacked_headers_.push_back(
        CompressedHeaderInfo(offset, data_length, ack_listener));
  }
}

}  // namespace net
