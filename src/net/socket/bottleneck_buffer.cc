// Copyright 2026 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/socket/bottleneck_buffer.h"

#include <algorithm>
#include <ranges>
#include <utility>

#include "base/check.h"
#include "base/check_op.h"
#include "base/functional/bind.h"
#include "base/numerics/safe_conversions.h"
#include "base/task/sequenced_task_runner.h"
#include "base/time/time.h"
#include "net/base/net_errors.h"

namespace net {

BottleneckBuffer::Chunk::Chunk(std::vector<uint8_t> data,
                               base::TimeTicks time_available)
    : data(std::move(data)), time_available(time_available) {}

BottleneckBuffer::Chunk::Chunk(Chunk&&) = default;
BottleneckBuffer::Chunk& BottleneckBuffer::Chunk::operator=(Chunk&&) = default;
BottleneckBuffer::Chunk::~Chunk() = default;

// static
size_t BottleneckBuffer::BdpCapacity(uint64_t throughput_bytes_per_sec,
                                     base::TimeDelta one_way_latency) {
  if (throughput_bytes_per_sec == 0 || !one_way_latency.is_positive()) {
    return kDefaultCapacity;
  }
  // BDP = throughput × RTT. `one_way_latency` is half-RTT, so RTT = 2×.
  // saturated_cast clamps absurdly large BDPs (e.g. 10 Gbps at 1s latency)
  // to SIZE_MAX instead of overflowing.
  double bdp = throughput_bytes_per_sec * 2.0 * one_way_latency.InSecondsF();
  size_t capacity = base::saturated_cast<size_t>(2 * bdp);
  return std::max(kMinCapacity, capacity);
}

BottleneckBuffer::BottleneckBuffer(base::TimeDelta one_way_latency,
                                   size_t capacity)
    : one_way_latency_(one_way_latency), capacity_(capacity) {
  CHECK_GT(capacity_, 0u);
  CHECK_GE(one_way_latency_, base::TimeDelta());
  // The buffer may be constructed before being handed to the socket
  // sequence that uses it. Bind the checker on first use instead of
  // construction.
  sequence_checker_.DetachFromSequence();
}

BottleneckBuffer::~BottleneckBuffer() {
  CheckValidSequence();
}

int BottleneckBuffer::Push(base::span<const uint8_t> data, Mode mode) {
  CheckValidSequence();
  if (data.empty()) {
    return 0;
  }

  const size_t data_size = data.size();
  const size_t space =
      capacity_ > buffered_bytes_ ? capacity_ - buffered_bytes_ : 0;

  if (mode == Mode::kDatagram) {
    if (data_size > capacity_) {
      // The datagram can never fit, even in an empty buffer.
      return ERR_MSG_TOO_BIG;
    }
    if (data_size > space) {
      // Doesn't fit right now; the caller should retry once Pull() frees
      // space and space_available_cb_ fires.
      return 0;
    }
  }

  if (space == 0) {
    return 0;
  }

  const size_t to_copy = std::min(space, data_size);

  base::TimeTicks now = base::TimeTicks::Now();
  base::TimeTicks available_at =
      one_way_latency_.is_positive() ? now + one_way_latency_ : now;

  auto copied = data.first(to_copy);
  std::vector<uint8_t> chunk_data(std::from_range, copied);
  chunks_.emplace_back(std::move(chunk_data), available_at);
  buffered_bytes_ += to_copy;

  // Ensure the drain timer is scheduled now that there is data to
  // deliver. ScheduleDrainTimer() is a no-op if a timer is already pending.
  ScheduleDrainTimer();

  return base::checked_cast<int>(to_copy);
}

int BottleneckBuffer::Pull(base::span<uint8_t> dest, Mode mode) {
  CheckValidSequence();
  if (chunks_.empty() || dest.empty()) {
    return 0;
  }

  const base::TimeTicks now = base::TimeTicks::Now();

  // Datagram mode: the ready front chunk must be delivered whole. If it
  // doesn't fit in `dest`, surface an error instead of splitting it.
  if (mode == Mode::kDatagram) {
    const Chunk& front = chunks_.front();
    if (now >= front.time_available && front.remaining() > dest.size()) {
      return ERR_MSG_TOO_BIG;
    }
  }

  const bool was_full = full();
  size_t total_pulled = 0;
  const size_t dest_size = dest.size();

  while (!chunks_.empty() && total_pulled < dest_size) {
    Chunk& front = chunks_.front();

    if (now < front.time_available) {
      break;
    }

    const size_t want = dest_size - total_pulled;
    const size_t can_copy = std::min(want, front.remaining());

    base::span<const uint8_t> src_span =
        base::span(front.data).subspan(front.offset, can_copy);
    dest.subspan(total_pulled, can_copy).copy_from(src_span);
    front.offset += can_copy;
    total_pulled += can_copy;
    buffered_bytes_ -= can_copy;

    if (front.remaining() == 0) {
      chunks_.pop_front();
    }

    if (mode == Mode::kDatagram) {
      break;
    }
  }

  if (was_full && !full()) {
    PostSpaceAvailable();
  }
  // Re-arm for any remaining chunks. ScheduleDrainTimer() is a no-op if the
  // buffer is now empty or a timer is already pending.
  ScheduleDrainTimer();
  return base::checked_cast<int>(total_pulled);
}

bool BottleneckBuffer::has_ready_data() const {
  CheckValidSequence();
  return !chunks_.empty() &&
         base::TimeTicks::Now() >= chunks_.front().time_available;
}

size_t BottleneckBuffer::GetReadyBytesAtFront(size_t max_bytes) const {
  CheckValidSequence();
  if (chunks_.empty() || max_bytes == 0) {
    return 0;
  }

  if (base::TimeTicks::Now() < chunks_.front().time_available) {
    return 0;
  }
  return std::min(max_bytes, chunks_.front().remaining());
}

void BottleneckBuffer::SetCallbacks(DataReadyCallback data_ready_cb,
                                    SpaceAvailableCallback space_available_cb) {
  CheckValidSequence();
  // Callbacks must be callable. Pass base::DoNothing() to suppress a signal
  // rather than a null callback.
  CHECK(!data_ready_cb.is_null());
  CHECK(!space_available_cb.is_null());
  data_ready_cb_ = std::move(data_ready_cb);
  space_available_cb_ = std::move(space_available_cb);
  // If data is already ready at registration time, fire data_ready_cb_
  // so a consumer that binds late doesn't miss the signal. We don't do
  // the equivalent for space_available_cb_ because every empty buffer
  // is trivially "not full" — firing that would always be a false
  // positive.
  if (has_ready_data()) {
    PostDataReady();
  }
}

void BottleneckBuffer::Reset() {
  CheckValidSequence();
  chunks_.clear();
  buffered_bytes_ = 0;
  drain_timer_.Stop();
  // Invalidate any callback tasks posted before this Reset() — the
  // consumer asked us to forget; they shouldn't observe spurious
  // "data ready" / "space available" signals from the old data.
  ++callback_generation_;
}

void BottleneckBuffer::CheckValidSequence() const {
  CHECK(sequence_checker_.CalledOnValidSequence());
}

void BottleneckBuffer::ScheduleDrainTimer() {
  CheckValidSequence();
  if (chunks_.empty() || drain_timer_.IsRunning()) {
    return;
  }

  base::TimeTicks now = base::TimeTicks::Now();
  const Chunk& front = chunks_.front();

  // Wait for latency if chunk isn't ready yet.
  if (now < front.time_available) {
    base::TimeDelta wait = front.time_available - now;
    drain_timer_.Start(FROM_HERE, wait,
                       base::BindOnce(&BottleneckBuffer::OnDrainTimer,
                                      weak_factory_.GetWeakPtr()));
    return;
  }

  // Front chunk is ready now. Post a zero-delay timer task so
  // OnDrainTimer fires on its own task rather than re-entering the
  // caller of ScheduleDrainTimer.
  drain_timer_.Start(FROM_HERE, base::TimeDelta(),
                     base::BindOnce(&BottleneckBuffer::OnDrainTimer,
                                    weak_factory_.GetWeakPtr()));
}

void BottleneckBuffer::OnDrainTimer() {
  CheckValidSequence();
  const bool ready = has_ready_data();
  if (ready) {
    PostDataReady();
  }
  // Re-schedule IFF the next chunk is not yet ready. Otherwise, the reader
  // consumes it synchronously, or schedules the drain timer itself.
  if (!chunks_.empty() && !ready) {
    ScheduleDrainTimer();
  }
}

void BottleneckBuffer::PostDataReady() {
  base::SequencedTaskRunner::GetCurrentDefault()->PostTask(
      FROM_HERE,
      base::BindOnce(&BottleneckBuffer::DispatchDataReady,
                     weak_factory_.GetWeakPtr(), callback_generation_));
}

void BottleneckBuffer::PostSpaceAvailable() {
  base::SequencedTaskRunner::GetCurrentDefault()->PostTask(
      FROM_HERE,
      base::BindOnce(&BottleneckBuffer::DispatchSpaceAvailable,
                     weak_factory_.GetWeakPtr(), callback_generation_));
}

void BottleneckBuffer::DispatchDataReady(uint64_t generation) {
  CheckValidSequence();
  // Drop tasks invalidated by an intervening Reset().
  if (generation != callback_generation_) {
    return;
  }
  data_ready_cb_.Run();
}

void BottleneckBuffer::DispatchSpaceAvailable(uint64_t generation) {
  CheckValidSequence();
  // Drop tasks invalidated by an intervening Reset().
  if (generation != callback_generation_) {
    return;
  }
  space_available_cb_.Run();
}

}  // namespace net
