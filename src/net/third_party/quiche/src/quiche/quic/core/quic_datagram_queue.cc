// Copyright 2019 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "quiche/quic/core/quic_datagram_queue.h"

#include "absl/types/span.h"
#include "quiche/quic/core/quic_constants.h"
#include "quiche/quic/core/quic_session.h"
#include "quiche/quic/core/quic_time.h"
#include "quiche/quic/core/quic_types.h"

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

MessageStatus QuicDatagramQueue::SendOrQueueDatagram(
    quiche::QuicheMemSlice datagram) {
  // If the queue is non-empty, always queue the daragram.  This ensures that
  // the datagrams are sent in the same order that they were sent by the
  // application.
  if (queue_.empty()) {
    MessageResult result = session_->SendMessage(absl::MakeSpan(&datagram, 1),
                                                 /*flush=*/force_flush_);
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

  MessageResult result =
      session_->SendMessage(absl::MakeSpan(&queue_.front().datagram, 1));
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
    ++expired_datagram_count_;
    queue_.pop_front();
    if (observer_) {
      observer_->OnDatagramProcessed(absl::nullopt);
    }
  }
}

}  // namespace quic
