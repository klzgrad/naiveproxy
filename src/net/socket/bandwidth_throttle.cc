// Copyright 2026 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/socket/bandwidth_throttle.h"

#include <algorithm>
#include <utility>

#include "base/auto_reset.h"
#include "base/check.h"
#include "base/check_op.h"
#include "base/functional/bind.h"
#include "base/memory/ref_counted.h"
#include "base/memory/scoped_refptr.h"
#include "base/time/time.h"

namespace net {

// --- CancellationHandle ---

BandwidthThrottle::CancellationHandle::CancellationHandle() = default;

BandwidthThrottle::CancellationHandle::CancellationHandle(
    scoped_refptr<base::RefCountedData<bool>> canceled_flag,
    base::OnceClosure on_cancel)
    : canceled_flag_(std::move(canceled_flag)),
      on_cancel_(std::move(on_cancel)) {}

BandwidthThrottle::CancellationHandle::CancellationHandle(
    CancellationHandle&& other) noexcept
    : canceled_flag_(std::move(other.canceled_flag_)),
      on_cancel_(std::move(other.on_cancel_)) {}

BandwidthThrottle::CancellationHandle&
BandwidthThrottle::CancellationHandle::operator=(
    CancellationHandle&& other) noexcept {
  if (this != &other) {
    Cancel();
    canceled_flag_ = std::move(other.canceled_flag_);
    on_cancel_ = std::move(other.on_cancel_);
  }
  return *this;
}

BandwidthThrottle::CancellationHandle::~CancellationHandle() {
  Cancel();
}

void BandwidthThrottle::CancellationHandle::Cancel() {
  if (canceled_flag_) {
    canceled_flag_->data = true;
    canceled_flag_.reset();
    if (on_cancel_) {
      std::move(on_cancel_).Run();
    }
  }
}

// --- PendingRequest ---

BandwidthThrottle::PendingRequest::PendingRequest(
    int bytes,
    ThrottleCallback callback,
    scoped_refptr<base::RefCountedData<bool>> canceled_flag)
    : bytes(bytes),
      callback(std::move(callback)),
      canceled_flag(std::move(canceled_flag)) {}

BandwidthThrottle::PendingRequest::PendingRequest(PendingRequest&&) = default;
BandwidthThrottle::PendingRequest& BandwidthThrottle::PendingRequest::operator=(
    PendingRequest&&) = default;
BandwidthThrottle::PendingRequest::~PendingRequest() = default;

// --- BandwidthThrottle ---

BandwidthThrottle::BandwidthThrottle(uint64_t throughput_bytes_per_sec,
                                     base::TimeDelta burst_duration)
    : throughput_bytes_per_sec_(throughput_bytes_per_sec),
      burst_size_(static_cast<double>(throughput_bytes_per_sec_) *
                  burst_duration.InSecondsF()),
      // Start the bucket full: the link is always at its configured
      // rate, so a freshly-constructed throttle's first request can
      // claim up to burst_size_ for free. See class doc.
      available_tokens_(burst_size_) {
  CHECK_GT(throughput_bytes_per_sec, 0u);
  CHECK_GT(burst_duration, base::TimeDelta());
  // The throttle is ref-counted and may be constructed before being handed to
  // the socket sequence that uses it. Bind the checker on first use instead of
  // construction.
  sequence_checker_.DetachFromSequence();
}

BandwidthThrottle::~BandwidthThrottle() {
  CheckValidSequence();
}

uint64_t BandwidthThrottle::throughput_bytes_per_sec() const {
  CheckValidSequence();
  return throughput_bytes_per_sec_;
}

BandwidthThrottle::CancellationHandle BandwidthThrottle::RequestBytes(
    int bytes,
    ThrottleCallback callback) {
  CheckValidSequence();
  CHECK_GT(bytes, 0);
  CHECK(!callback.is_null());

  // Refill *before* enqueuing this request.
  //
  // `available_tokens_` has two physical meanings depending on whether
  // the queue is empty:
  //   - Empty queue: tokens are "idle credit" — unused link capacity
  //     that can fuel a future burst. Capped at burst_size_ so an
  //     unused throttle doesn't store unbounded burst potential.
  //   - Non-empty queue: tokens are "transmission progress" toward the
  //     front request — the link is actively serving that request and
  //     tokens are allowed to grow past burst_size_ so a request larger
  //     than burst_size_ can eventually complete.
  //
  // RefillTokens() picks which interpretation to use by reading the
  // queue state when it's called. If we enqueued first and refilled
  // after, an idle-period's worth of new tokens would be reclassified
  // as transmission progress and accumulate uncapped — a request
  // arriving after a long idle would then be handed the entire idle
  // duration as instantaneous burst. Refilling first applies the cap
  // while the queue is still empty.
  RefillTokens();

  auto canceled_flag = base::MakeRefCounted<base::RefCountedData<bool>>(false);
  // Always queue and complete via the drain timer callback on a separate task,
  // even when tokens are immediately available. Callers can therefore use this
  // without worrying about reentrancy into their Read/Write contracts.
  pending_requests_.emplace(bytes, std::move(callback), canceled_flag);
  // Re-entrant calls (RequestBytes from inside a fired callback) are
  // handled by ProcessQueue's loop continuation; don't post a redundant
  // zero-delay timer task that would fire on an empty queue.
  if (!processing_queue_ && !drain_timer_.IsRunning()) {
    ScheduleDrainTimer();
  }
  return CancellationHandle(
      std::move(canceled_flag),
      base::BindOnce(&BandwidthThrottle::OnCancellationHandleCancelled,
                     weak_factory_.GetWeakPtr()));
}

void BandwidthThrottle::OnCancellationHandleCancelled() {
  CheckValidSequence();
  // A sibling cancellation from inside a fired ThrottleCallback would
  // have us tear down the very timer that's about to be re-armed by
  // ProcessQueue's loop continuation. Skip and let ProcessQueue handle
  // it when it returns.
  if (processing_queue_) {
    return;
  }
  // The drain timer was sized for the request that just got cancelled.
  // Stop it and reschedule so a smaller successor isn't blocked behind
  // the now-irrelevant wait.
  drain_timer_.Stop();
  ScheduleDrainTimer();
}

void BandwidthThrottle::CheckValidSequence() const {
  CHECK(sequence_checker_.CalledOnValidSequence());
}

void BandwidthThrottle::RefillTokens() {
  base::TimeTicks now = base::TimeTicks::Now();

  if (last_refill_time_.is_null()) {
    last_refill_time_ = now;
    return;
  }

  base::TimeDelta elapsed = now - last_refill_time_;
  if (elapsed.is_positive()) {
    double new_tokens = throughput_bytes_per_sec_ * elapsed.InSecondsF();
    available_tokens_ += new_tokens;
    // Cap only when the queue is empty ("idle credit" mode): an unused
    // throttle must not bank arbitrary future burst. When the queue is
    // non-empty ("transmission progress" mode) we deliberately let
    // tokens grow past burst_size_ so a request larger than burst_size_
    // can accumulate the bytes it needs.
    if (pending_requests_.empty() && available_tokens_ > burst_size_) {
      available_tokens_ = burst_size_;
    }
    last_refill_time_ = now;
  }
}

void BandwidthThrottle::DropCancelledFrontRequests() {
  while (!pending_requests_.empty() &&
         pending_requests_.front().canceled_flag->data) {
    pending_requests_.pop();
  }
}

void BandwidthThrottle::ProcessQueue() {
  RefillTokens();

  // Pin `this` so a re-entrant callback that drops its last reference does
  // not pull the throttle out from under us mid-loop.
  scoped_refptr<BandwidthThrottle> self(this);
  base::AutoReset<bool> guard(&processing_queue_, true);

  while (!pending_requests_.empty()) {
    // Skip cancelled requests without charging tokens.
    if (pending_requests_.front().canceled_flag->data) {
      pending_requests_.pop();
      continue;
    }
    PendingRequest& front = pending_requests_.front();
    if (available_tokens_ < front.bytes) {
      // Not enough tokens. Schedule timer for when we'll have enough.
      ScheduleDrainTimer();
      return;
    }
    available_tokens_ -= front.bytes;
    // Pop *before* invoking the callback so a re-entrant call into the
    // throttle (e.g. another RequestBytes from inside the callback) sees a
    // consistent queue state.
    ThrottleCallback callback = std::move(front.callback);
    pending_requests_.pop();
    std::move(callback).Run();
  }
}

void BandwidthThrottle::ScheduleDrainTimer() {
  // Drop cancelled head entries so we don't waste timer time waiting for
  // tokens that no live request will consume.
  DropCancelledFrontRequests();
  if (pending_requests_.empty() || drain_timer_.IsRunning()) {
    return;
  }

  RefillTokens();

  double deficit = pending_requests_.front().bytes - available_tokens_;
  // If we already have enough tokens, post a zero-delay timer so the
  // completion still fires on a separate task. Otherwise wait long enough
  // to accumulate the remaining tokens. Clamp positive wait durations to
  // a minimum of 1 µs so a tiny float deficit at gigabit-class rates
  // doesn't truncate to a zero-delay task and spin the message loop.
  base::TimeDelta wait_time;
  if (deficit > 0) {
    wait_time = std::max(base::Microseconds(1),
                         base::Seconds(deficit / throughput_bytes_per_sec_));
  }
  drain_timer_.Start(FROM_HERE, wait_time,
                     base::BindOnce(&BandwidthThrottle::OnDrainTimer,
                                    weak_factory_.GetWeakPtr()));
}

void BandwidthThrottle::OnDrainTimer() {
  CheckValidSequence();
  ProcessQueue();
}

}  // namespace net
