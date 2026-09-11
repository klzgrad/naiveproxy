// Copyright 2026 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef QUICHE_QUIC_MOQT_MOQT_CONTROL_MESSAGE_QUEUE_H_
#define QUICHE_QUIC_MOQT_MOQT_CONTROL_MESSAGE_QUEUE_H_

#include <cstddef>

#include "absl/base/nullability.h"
#include "absl/status/status.h"
#include "quiche/common/platform/api/quiche_export.h"
#include "quiche/common/quiche_buffer_allocator.h"
#include "quiche/common/quiche_circular_deque.h"
#include "quiche/web_transport/web_transport.h"

namespace moqt {

// MoqtControlMessageQueue manages the queueing and sending of outgoing control
// messages on a WebTransport stream.
class QUICHE_EXPORT MoqtControlMessageQueue {
 public:
  // Maximum amount of messages buffered on top of the QUIC send buffer.
  static constexpr size_t kMaxPendingMessages = 100;

  MoqtControlMessageQueue() = default;
  explicit MoqtControlMessageQueue(webtransport::Stream* absl_nullable stream)
      : stream_(stream) {}

  webtransport::Stream* absl_nullable stream() const { return stream_; }
  void SetStream(webtransport::Stream* absl_nonnull stream) {
    stream_ = stream;
  }

  bool QueueIsFull() const {
    return pending_messages_.size() == kMaxPendingMessages;
  }

  absl::Status SendOrBufferMessage(quiche::QuicheBuffer message,
                                   bool fin = false);
  absl::Status Fin();

  // Dequeues all pending writes.
  absl::Status OnCanWrite();

 private:
  absl::Status AddToQueue(quiche::QuicheBuffer message);
  static absl::Status SendMessage(webtransport::Stream& stream,
                                  quiche::QuicheBuffer message, bool fin);

  webtransport::Stream* absl_nullable stream_ = nullptr;
  quiche::QuicheCircularDeque<quiche::QuicheBuffer> pending_messages_;
  bool fin_queued_ = false;
};

}  // namespace moqt

#endif  // QUICHE_QUIC_MOQT_MOQT_CONTROL_MESSAGE_QUEUE_H_
