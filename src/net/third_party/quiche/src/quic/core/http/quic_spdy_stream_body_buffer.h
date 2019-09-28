// Copyright (c) 2018 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef QUICHE_QUIC_CORE_HTTP_QUIC_SPDY_STREAM_BODY_BUFFER_H_
#define QUICHE_QUIC_CORE_HTTP_QUIC_SPDY_STREAM_BODY_BUFFER_H_

#include "net/third_party/quiche/src/quic/core/http/http_decoder.h"
#include "net/third_party/quiche/src/quic/platform/api/quic_bug_tracker.h"
#include "net/third_party/quiche/src/quic/platform/api/quic_containers.h"
#include "net/third_party/quiche/src/quic/platform/api/quic_export.h"
#include "net/third_party/quiche/src/quic/platform/api/quic_iovec.h"
#include "net/third_party/quiche/src/quic/platform/api/quic_macros.h"

namespace quic {

class QuicStreamSequencer;

// Buffer decoded body for QuicSpdyStream. It also talks to the sequencer to
// consume data.
class QUIC_EXPORT_PRIVATE QuicSpdyStreamBodyBuffer {
 public:
  QuicSpdyStreamBodyBuffer();
  ~QuicSpdyStreamBodyBuffer() = default;

  // Add metadata of the frame to accountings.
  // Called when QuicSpdyStream receives data frame header.
  void OnDataHeader(Http3FrameLengths frame_lengths);

  // Add new data payload to buffer.
  // Called when QuicSpdyStream received data payload.
  // Data pointed by payload must be alive until consumed by
  // QuicStreamSequencer::MarkConsumed().
  void OnDataPayload(QuicStringPiece payload);

  // Internally marks |num_bytes| of DATA frame payload consumed, and returns
  // the number of bytes that the caller should mark consumed with the
  // sequencer, including DATA frame header bytes, if any.
  QUIC_MUST_USE_RESULT size_t OnBodyConsumed(size_t num_bytes);

  // Fill up to |iov_len| with bodies available in buffer. No data is consumed.
  // |iov|.iov_base will point to data in the buffer, and |iov|.iov_len will
  // be set to the underlying data length accordingly.
  // Returns the number of iov used.
  int PeekBody(iovec* iov, size_t iov_len) const;

  // Copies from buffer into |iov| up to |iov_len|, and consume data in
  // sequencer. |iov.iov_base| and |iov.iov_len| are preassigned and will not be
  // changed.  |*total_bytes_read| is set to the number of bytes read.  Returns
  // the number of bytes that should be marked consumed with the sequencer.
  QUIC_MUST_USE_RESULT size_t ReadBody(const struct iovec* iov,
                                       size_t iov_len,
                                       size_t* total_bytes_read);

  bool HasBytesToRead() const { return !bodies_.empty(); }

  uint64_t total_body_bytes_received() const {
    return total_body_bytes_received_;
  }

 private:
  // Storage for decoded data.
  QuicDeque<QuicStringPiece> bodies_;
  // Storage for header lengths.
  QuicDeque<Http3FrameLengths> frame_meta_;
  // Bytes in the first available data frame that are not consumed yet.
  QuicByteCount bytes_remaining_;
  // Total available body data in the stream.
  QuicByteCount total_body_bytes_readable_;
  // Total bytes read from the stream excluding headers.
  QuicByteCount total_body_bytes_received_;
  // Total length of payloads tracked by frame_meta_.
  QuicByteCount total_payload_lengths_;
};

}  // namespace quic

#endif  // QUICHE_QUIC_CORE_HTTP_QUIC_SPDY_STREAM_BODY_BUFFER_H_
