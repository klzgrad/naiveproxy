// Copyright 2019 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "quic/core/quic_datagram_queue.h"

#include "quic/core/quic_constants.h"
#include "quic/core/quic_session.h"
#include "quic/core/quic_time.h"
#include "quic/core/quic_types.h"
#include "quic/platform/api/quic_mem_slice_span.h"

namespace quic {


constexpr float kExpiryInMinRtts = 1.25;
constexpr float kMinPacingWindows = 4;

QuicDatagramQueue::QuicDatagramQueue(QuicSession* session)
    : QuicDatagramQueue(session, nullptr) {}

QuicDatagramQueue::QuicDatagramQueue(QuicSession* session,
                                     std::unique_ptr<Observer> observer)
    : session_(session),
      clock_(session->connection()->clock()),
      observer_(std::move(observer)) {}

MessageStatus QuicDatagramQueue::SendOrQueueDatagram(QuicMemSlice datagram) {
  // If the queue is non-empty, always queue the daragram.  This ensures that
  // the datagrams are sent in the same order that they were sent by the
  // application.
  if (queue_.empty()) {
    QuicMemSliceSpan span(&datagram);
    MessageResult result = session_->SendMessage(span);
    if (result.status != MESSAGE_STATUS_BLOCKED) {
      if (observer_) {
        observer_->OnDatagramProcessed(result.status);
      }
      return result.status;
    }
  }

  queue_.emplace_back(Datagram{std::move(datagram),
                               clock_->ApproximateNow() + GetMaxTimeInQueue()});
  return MESSAGE_STATUS_BLOCKED;
}

absl::optional<MessageStatus> QuicDatagramQueue::TrySendingNextDatagram() {
  RemoveExpiredDatagrams();
  if (queue_.empty()) {
    return absl::nullopt;
  }

  QuicMemSliceSpan span(&queue_.front().datagram);
  MessageResult result = session_->SendMessage(span);
  if (result.status != MESSAGE_STATUS_BLOCKED) {
    queue_.pop_front();
    if (observer_) {
      observer_->OnDatagramProcessed(result.status);
    }
  }
  return result.status;
}

size_t QuicDatagramQueue::SendDatagrams() {
  size_t num_datagrams = 0;
  for (;;) {
    absl::optional<MessageStatus> status = TrySendingNextDatagram();
    if (!status.has_value()) {
      break;
    }
    if (*status == MESSAGE_STATUS_BLOCKED) {
      break;
    }
    num_datagrams++;
  }
  return num_datagrams;
}

QuicTime::Delta QuicDatagramQueue::GetMaxTimeInQueue() const {
  if (!max_time_in_queue_.IsZero()) {
    return max_time_in_queue_;
  }

  const QuicTime::Delta min_rtt =
      session_->connection()->sent_packet_manager().GetRttStats()->min_rtt();
  return std::max(kExpiryInMinRtts * min_rtt,
                  kMinPacingWindows * kAlarmGranularity);
}

void QuicDatagramQueue::RemoveExpiredDatagrams() {
  QuicTime now = clock_->ApproximateNow();
  while (!queue_.empty() && queue_.front().expiry <= now) {
    queue_.pop_front();
    if (observer_) {
      observer_->OnDatagramProcessed(absl::nullopt);
    }
  }
}

}  // namespace quic
