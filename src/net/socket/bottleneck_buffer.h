// Copyright 2026 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef NET_SOCKET_BOTTLENECK_BUFFER_H_
#define NET_SOCKET_BOTTLENECK_BUFFER_H_

#include <stddef.h>
#include <stdint.h>

#include <vector>

#include "base/containers/circular_deque.h"
#include "base/containers/span.h"
#include "base/functional/callback.h"
#include "base/memory/weak_ptr.h"
#include "base/sequence_checker_impl.h"
#include "base/time/time.h"
#include "base/timer/timer.h"
#include "net/base/net_export.h"

namespace net {

// An intermediate per-socket bottleneck buffer that models the propagation
// delay (one-way latency) of a network link for one stream direction.
//
// Data enters the buffer instantly (as fast as the producer can push it).
// Each chunk is tagged with a "time_available" timestamp =
// arrival_time + one_way_latency, modelling propagation delay. The
// consumer can pull a chunk once that timestamp has elapsed.
//
// Bandwidth is shaped by a separate, shareable BandwidthThrottle outside
// this buffer; per-buffer throughput is intentionally NOT modelled here.
//
// Callers are meant to own separate download and upload buffers.
// The buffer has a fixed capacity (default 2MB per direction). When
// full, Push() returns 0 (or a partial count) so the producer naturally
// backs off; once Pull() frees space, `space_available_cb_` fires so the
// producer can resume. This backpressure propagates to the OS socket's
// receive window for reads, or blocks application writes for uploads. A
// separate `data_ready_cb_` fires as an "invitation to drain" whenever
// the buffer has consumable data and the consumer hasn't been re-notified
// by an intervening Pull(). It may fire more than once for the same
// front chunk (e.g., when a fresh Push() arrives while ready data is
// still buffered, the drain timer re-arms and the callback fires again).
// Treat each fire as a hint to call Pull(); don't rely on it as a strict
// empty-to-ready edge.
//
// Stream- vs datagram-oriented consumers select how chunk boundaries and
// capacity are handled via the Mode argument to Push()/Pull(). Mode::
// kDatagram never splits a message: Push() accepts a datagram whole or not
// at all, and Pull() delivers one whole chunk or returns an error rather
// than truncating it.
//
// Sizes and capacities are tracked as size_t (matching span/std::vector
// arithmetic, which keeps the implementation cast-free). Push() and Pull()
// return int to match net/'s Socket read/write convention: a non-negative
// byte count, or a negative net::Error (e.g. net::ERR_MSG_TOO_BIG).
//
// Thread-unsafe: all calls must happen on the same sequence.
class NET_EXPORT BottleneckBuffer {
 public:
  // Minimum buffer capacity.
  static constexpr size_t kMinCapacity = 16 * 1024;  // 16KB

  // Fallback when BDP cannot be computed. For latency-only streams this
  // is a finite memory/backpressure cap, not a network bandwidth model.
  // Keep it large enough to avoid throttling common high-throughput
  // links; 2MB can absorb well over 50Mbps of in-flight data at 100ms
  // latency.
  static constexpr size_t kDefaultCapacity = 2 * 1024 * 1024;  // 2MB

  // Computes a capacity based on the bandwidth-delay product (BDP):
  // the amount of data that can be "in the pipe" at any given time.
  // Returns 2× BDP so the producer can fill the next window while the
  // consumer drains the current one. Falls back to kDefaultCapacity when
  // `throughput_bytes_per_sec` is 0 or `one_way_latency` is non-positive.
  // `throughput_bytes_per_sec` is only used as a sizing hint here; the
  // buffer itself does not enforce throughput at runtime.
  static size_t BdpCapacity(uint64_t throughput_bytes_per_sec,
                            base::TimeDelta one_way_latency);

  // Invitation to drain: fires when ready data is available. See the
  // class doc for level-ish-vs-edge-triggered semantics.
  using DataReadyCallback = base::RepeatingClosure;

  // Fires on the full→not-full transition.
  using SpaceAvailableCallback = base::RepeatingClosure;

  // Whether the consumer treats the byte flow as a continuous stream or as
  // discrete datagrams. The same choice applies to both directions, so a
  // single mode controls chunk-boundary and capacity handling in Push()
  // and Pull().
  enum class Mode {
    // Byte-stream (e.g., TCP): message boundaries don't matter.
    //   * Push() accepts as many bytes as fit and returns that count
    //     (0 when full); a partial count is backpressure.
    //   * Pull() drains ready chunks into `dest` across chunk boundaries
    //     until `dest` is full or no more chunks are ready, concatenating
    //     bytes from consecutive ready chunks.
    kStream,

    // Datagram (e.g., UDP): a message is never split.
    //   * Push() accepts the whole span atomically or nothing: it returns
    //     the full size on success, 0 if it doesn't fit right now (retry
    //     after `space_available_cb_`), or net::ERR_MSG_TOO_BIG if it can
    //     never fit even in an empty buffer (larger than capacity).
    //   * Pull() delivers exactly one chunk, preserving the producer's
    //     message boundary. If the ready front chunk does not fit entirely
    //     in `dest`, Pull() returns net::ERR_MSG_TOO_BIG and consumes
    //     nothing, rather than truncating the message.
    kDatagram,
  };

  // `one_way_latency` is the propagation delay added to each chunk.
  // Must be non-negative.
  // `capacity` is the buffer size in bytes; must be strictly positive.
  explicit BottleneckBuffer(base::TimeDelta one_way_latency,
                            size_t capacity = kDefaultCapacity);

  ~BottleneckBuffer();

  BottleneckBuffer(const BottleneckBuffer&) = delete;
  BottleneckBuffer& operator=(const BottleneckBuffer&) = delete;

  // Pushes data into the buffer. `mode` controls the behaviour when `data`
  // doesn't fit in the remaining capacity; see Mode for details.
  // Returns the number of bytes accepted (>= 0), or a negative net::Error
  // (net::ERR_MSG_TOO_BIG in kDatagram mode when `data` is larger than the
  // total capacity).
  int Push(base::span<const uint8_t> data, Mode mode = Mode::kStream);

  // Pulls "ready" data (latency elapsed) into `dest`. Returns the number
  // of bytes copied (>= 0), 0 if no data is ready yet, or a negative
  // net::Error (net::ERR_MSG_TOO_BIG in kDatagram mode when the ready front
  // chunk is larger than `dest`). `mode` controls whether the pull crosses
  // chunk boundaries; see Mode for details.
  int Pull(base::span<uint8_t> dest, Mode mode = Mode::kStream);

  // Returns the number of bytes currently buffered (including
  // not-yet-ready chunks).
  size_t buffered_bytes() const {
    CheckValidSequence();
    return buffered_bytes_;
  }

  // Returns true if the buffer has no data.
  bool empty() const {
    CheckValidSequence();
    return buffered_bytes_ == 0;
  }

  // Returns true if the buffer cannot accept more data.
  bool full() const {
    CheckValidSequence();
    return buffered_bytes_ >= capacity_;
  }

  // Returns the number of bytes of free space.
  size_t free_space() const {
    CheckValidSequence();
    return buffered_bytes_ < capacity_ ? capacity_ - buffered_bytes_ : 0;
  }

  // Returns true if there is data ready to be pulled (latency elapsed).
  bool has_ready_data() const;

  // Returns how many bytes can be pulled from the front chunk, capped at
  // `max_bytes`. Returns 0 if the front chunk is not ready yet, or if
  // `max_bytes` is 0.
  size_t GetReadyBytesAtFront(size_t max_bytes) const;

  // Installs the async callbacks. Must be called before the buffer is
  // driven (Push()/Pull()), since the drain timer may otherwise fire with
  // no callback to run. May be called more than once; each call replaces
  // both previously-installed callbacks.
  // Neither callback may be null — pass base::DoNothing() to suppress a
  // signal. base::DoNothing() overwrites with a no-op; it does NOT
  // preserve the previously-installed callback.
  //
  // Delivery contract for both callbacks:
  //   * Invoked on a posted task, not synchronously from inside a
  //     buffer method. The callback is therefore free to destroy the
  //     owner of this buffer (and the buffer with it) without UAF in
  //     any buffer method still on the stack.
  //   * BottleneckBuffer::Reset() invalidates any callback that has been
  //     posted but not yet dispatched.
  //
  // `data_ready_cb` is an invitation to drain: it fires whenever the
  // buffer has ready data the consumer may want to pull. It can fire
  // more than once for the same buffered data (see the class doc).
  // If data is *already* ready at the moment SetCallbacks() is
  // invoked, `data_ready_cb` is posted immediately to avoid a
  // lost-wakeup for late-binding consumers.
  //
  // `space_available_cb` fires only on the full→not-full transition;
  // it is not invoked at registration time even if the buffer is
  // currently not-full (a freshly-constructed empty buffer would
  // otherwise generate a confusing false positive).
  void SetCallbacks(DataReadyCallback data_ready_cb,
                    SpaceAvailableCallback space_available_cb);

  // Discards buffered data and stops the drain timer.
  // Does NOT clear callbacks or rebind the sequence checker, so the buffer can
  // be reused on the same sequence with the same callback wiring. Any
  // callback posted before this reset but not yet dispatched is silently
  // dropped. Does NOT fire `space_available_cb_` even though the buffer
  // transitions to empty.
  void Reset();

  size_t capacity() const {
    CheckValidSequence();
    return capacity_;
  }

 private:
  struct Chunk {
    Chunk(std::vector<uint8_t> data, base::TimeTicks time_available);
    Chunk(Chunk&&);
    Chunk& operator=(Chunk&&);
    ~Chunk();

    std::vector<uint8_t> data;
    // Offset into `data` for partially-consumed chunks.
    size_t offset = 0;
    // Earliest time this chunk can be drained.
    base::TimeTicks time_available;

    size_t remaining() const { return data.size() - offset; }
  };

  void CheckValidSequence() const;

  // Schedules the drain timer for the next available chunk.
  void ScheduleDrainTimer();
  void OnDrainTimer();

  void PostDataReady();
  void PostSpaceAvailable();
  void DispatchDataReady(uint64_t generation);
  void DispatchSpaceAvailable(uint64_t generation);

  const base::TimeDelta one_way_latency_;
  const size_t capacity_;

  // FIFO queue of buffered chunks.
  base::circular_deque<Chunk> chunks_;
  size_t buffered_bytes_ = 0;

  // Installed by SetCallbacks() before the buffer is driven. SetCallbacks()
  // rejects null, so once installed these are always callable.
  DataReadyCallback data_ready_cb_;
  SpaceAvailableCallback space_available_cb_;

  // Generation counter for posted callbacks. Reset() increments
  // `callback_generation_`, invalidating any in-flight dispatch* tasks
  // tagged with an earlier generation.
  uint64_t callback_generation_ = 0;

  base::OneShotTimer drain_timer_;

  // Binds on first use and CHECKs that all later calls use the same sequence.
  // Used directly rather than via SEQUENCE_CHECKER() so the check stays live in
  // release: a cross-sequence call on this lock-free state is a memory-safety
  // bug, and the macro compiles to a no-op outside DCHECK builds.
  mutable base::SequenceCheckerImpl sequence_checker_;

  base::WeakPtrFactory<BottleneckBuffer> weak_factory_{this};
};

}  // namespace net

#endif  // NET_SOCKET_BOTTLENECK_BUFFER_H_
