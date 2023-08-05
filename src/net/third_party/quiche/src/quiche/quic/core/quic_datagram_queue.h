// Copyright 2019 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef QUICHE_QUIC_CORE_QUIC_DATAGRAM_QUEUE_H_
#define QUICHE_QUIC_CORE_QUIC_DATAGRAM_QUEUE_H_

#include <memory>

#include "absl/types/optional.h"
#include "quiche/quic/core/quic_time.h"
#include "quiche/quic/core/quic_types.h"
#include "quiche/common/platform/api/quiche_mem_slice.h"
#include "quiche/common/quiche_circular_deque.h"

namespace quic {

class QuicSession;

// Provides a way to buffer QUIC datagrams (messages) in case they cannot
// be sent due to congestion control.  Datagrams are buffered for a limited
// amount of time, and deleted after that time passes.
class QUIC_EXPORT_PRIVATE QuicDatagramQueue {
 public:
  // An interface used to monitor events on the associated `QuicDatagramQueue`.
  class QUIC_EXPORT_PRIVATE Observer {
   public:
    virtual ~Observer() = default;

    // Called when a datagram in the associated queue is sent or discarded.
    // Identity information for the datagram is not given, because the sending
    // and discarding order is always first-in-first-out.
    // This function is called synchronously in `QuicDatagramQueue` methods.
    // `status` is nullopt when the datagram is dropped due to being in the
    // queue for too long.
    virtual void OnDatagramProcessed(absl::optional<MessageStatus> status) = 0;
  };

  // |session| is not owned and must outlive this object.
  explicit QuicDatagramQueue(QuicSession* session);

  // |session| is not owned and must outlive this object.
  QuicDatagramQueue(QuicSession* session, std::unique_ptr<Observer> observer);

  // Adds the datagram to the end of the queue.  May send it immediately; if
  // not, MESSAGE_STATUS_BLOCKED is returned.
  MessageStatus SendOrQueueDatagram(quiche::QuicheMemSlice datagram);

  // Attempts to send a single datagram from the queue.  Returns the result of
  // SendMessage(), or nullopt if there were no unexpired datagrams to send.
  absl::optional<MessageStatus> TrySendingNextDatagram();

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

  // If set to true, all datagrams added into the queue would be sent with the
  // flush flag set to true, meaning that they will bypass congestion control
  // and related logic.
  void SetForceFlush(bool force_flush) { force_flush_ = force_flush; }

  size_t queue_size() { return queue_.size(); }

  bool empty() { return queue_.empty(); }

 private:
  struct QUIC_EXPORT_PRIVATE Datagram {
    quiche::QuicheMemSlice datagram;
    QuicTime expiry;
  };

  // Removes expired datagrams from the front of the queue.
  void RemoveExpiredDatagrams();

  QuicSession* session_;  // Not owned.
  const QuicClock* clock_;

  QuicTime::Delta max_time_in_queue_ = QuicTime::Delta::Zero();
  quiche::QuicheCircularDeque<Datagram> queue_;
  std::unique_ptr<Observer> observer_;
  bool force_flush_;
};

}  // namespace quic

#endif  // QUICHE_QUIC_CORE_QUIC_DATAGRAM_QUEUE_H_
