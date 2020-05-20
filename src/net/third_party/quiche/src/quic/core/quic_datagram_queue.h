// Copyright 2019 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef QUICHE_QUIC_CORE_QUIC_DATAGRAM_QUEUE_H_
#define QUICHE_QUIC_CORE_QUIC_DATAGRAM_QUEUE_H_

#include "net/third_party/quiche/src/quic/core/quic_circular_deque.h"
#include "net/third_party/quiche/src/quic/core/quic_time.h"
#include "net/third_party/quiche/src/quic/core/quic_types.h"
#include "net/third_party/quiche/src/quic/platform/api/quic_mem_slice.h"
#include "net/third_party/quiche/src/common/platform/api/quiche_optional.h"

namespace quic {

class QuicSession;

// Provides a way to buffer QUIC datagrams (messages) in case they cannot
// be sent due to congestion control.  Datagrams are buffered for a limited
// amount of time, and deleted after that time passes.
class QUIC_EXPORT_PRIVATE QuicDatagramQueue {
 public:
  // |session| is not owned and must outlive this object.
  explicit QuicDatagramQueue(QuicSession* session);

  // Adds the datagram to the end of the queue.  May send it immediately; if
  // not, MESSAGE_STATUS_BLOCKED is returned.
  MessageStatus SendOrQueueDatagram(QuicMemSlice datagram);

  // Attempts to send a single datagram from the queue.  Returns the result of
  // SendMessage(), or nullopt if there were no unexpired datagrams to send.
  quiche::QuicheOptional<MessageStatus> TrySendingNextDatagram();

  // Sends all of the unexpired datagrams until either the connection becomes
  // write-blocked or the queue is empty.  Returns the number of datagrams sent.
  size_t SendDatagrams();

  // Returns the amount of time a datagram is allowed to be in the queue before
  // it is dropped.  If not set explicitly using SetMaxTimeInQueue(), an
  // RTT-based heuristic is used.
  QuicTime::Delta GetMaxTimeInQueue() const;

  void SetMaxTimeInQueue(QuicTime::Delta max_time_in_queue) {
    max_time_in_queue_ = max_time_in_queue;
  }

  size_t queue_size() { return queue_.size(); }

  bool empty() { return queue_.empty(); }

 private:
  struct QUIC_EXPORT_PRIVATE Datagram {
    QuicMemSlice datagram;
    QuicTime expiry;
  };

  // Removes expired datagrams from the front of the queue.
  void RemoveExpiredDatagrams();

  QuicSession* session_;  // Not owned.
  const QuicClock* clock_;

  QuicTime::Delta max_time_in_queue_ = QuicTime::Delta::Zero();
  QuicCircularDeque<Datagram> queue_;
};

}  // namespace quic

#endif  // QUICHE_QUIC_CORE_QUIC_DATAGRAM_QUEUE_H_
