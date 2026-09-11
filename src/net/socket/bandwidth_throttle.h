// Copyright 2026 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef NET_SOCKET_BANDWIDTH_THROTTLE_H_
#define NET_SOCKET_BANDWIDTH_THROTTLE_H_

#include <stdint.h>

#include "base/containers/queue.h"
#include "base/functional/callback.h"
#include "base/memory/ref_counted.h"
#include "base/memory/scoped_refptr.h"
#include "base/memory/weak_ptr.h"
#include "base/sequence_checker_impl.h"
#include "base/time/time.h"
#include "base/timer/timer.h"
#include "net/base/net_export.h"

namespace base {
template <typename T>
class RefCountedData;
}

namespace net {

// A shared token-bucket bandwidth throttle. Multiple sockets draw from the
// same throttle to simulate a single constrained network link.
//
// Tokens represent bytes. They accumulate at `throughput_bytes_per_sec`
// bytes/sec up to a maximum of `burst_size` bytes. The bucket starts
// full, as this models the link's bandwidth, not congestion control mechanisms.
// Requests are always completed on a posted task — zero delay when tokens are
// immediately available, otherwise after enough tokens have accumulated.
// Posting completion (rather than running it inline) lets callers safely chain
// the throttle into their own Read/Write contracts without reentrancy
// concerns.
//
// Requests are served FIFO. A queued request blocks subsequent requests even
// if a later, smaller request could be served — this prevents starvation and
// models a real link where bytes are delivered in order.
//
// Cancellation: RequestBytes() returns a CancellationHandle. Destroying or
// explicitly cancelling it removes the request from the queue and
// prevents it from charging tokens. Sockets that own a CancellationHandle get
// automatic cancellation on destruction — this is critical because the
// throttle is shared across sockets, and a destroyed socket's pending
// request would otherwise steal tokens from live sockets.
//
// The pending-requests queue is unbounded; the throttle relies on its
// callers for backpressure. The expected pattern is one in-flight
// RequestBytes() per logical flow: hold a single CancellationHandle at a
// time and only enqueue the next byte request after the previous
// callback has fired. Under that discipline the queue size is bounded
// by the number of active flows sharing the throttle (each socket
// using one or two flows).
//
// A misbehaving caller that issues unbounded concurrent
// RequestBytes() calls would grow the queue without bound, consuming
// memory and — because the queue is FIFO — delaying every other
// flow's progress behind their backlog. The throttle does not defend
// against this; bounding is the caller's responsibility, similar to a
// caller-owned write buffer.
//
// Thread-unsafe: all calls must happen on the same sequence.
class NET_EXPORT BandwidthThrottle
    : public base::RefCounted<BandwidthThrottle> {
 public:
  REQUIRE_ADOPTION_FOR_REFCOUNTED_TYPE();

  // Callback invoked when a RequestBytes() request has been admitted.
  using ThrottleCallback = base::OnceClosure;

  // Movable RAII handle to a pending request. Destroying or explicitly
  // calling Cancel() removes the request from the throttle's queue and
  // prevents it from charging tokens. Cancellation is safe across
  // throttle destruction: the cancellation flag is shared between the
  // throttle's queue entry and this handle via scoped_refptr.
  //
  // Default-constructed handles are no-op. CancellationHandles are
  // sequence-affine: Cancel() and the destructor must run on the same
  // sequence as the throttle they came from.
  class NET_EXPORT CancellationHandle {
   public:
    CancellationHandle();
    CancellationHandle(CancellationHandle&&) noexcept;
    CancellationHandle& operator=(CancellationHandle&&) noexcept;
    CancellationHandle(const CancellationHandle&) = delete;
    CancellationHandle& operator=(const CancellationHandle&) = delete;
    ~CancellationHandle();

    // Cancels the associated request. Idempotent. Does nothing if the
    // request already completed (the entry is already popped) or this
    // handle is default-constructed / moved-from. Triggers the
    // throttle to re-evaluate its drain timer so a small successor
    // isn't starved by the cancelled head's now-stale wait.
    void Cancel();

   private:
    friend class BandwidthThrottle;
    CancellationHandle(scoped_refptr<base::RefCountedData<bool>> canceled_flag,
                       base::OnceClosure on_cancel);
    scoped_refptr<base::RefCountedData<bool>> canceled_flag_;
    // Invoked exactly once when this handle is cancelled (whether
    // by an explicit Cancel() call or by destruction). Bound to a
    // BandwidthThrottle::OnCancellationHandleCancelled method via WeakPtr so
    // it's a no-op after the throttle has been destroyed.
    base::OnceClosure on_cancel_;
  };

  // `throughput_bytes_per_sec` is the sustained throughput in bytes/sec.
  // Must be strictly positive. Callers that want an unconstrained link
  // should not construct a BandwidthThrottle at all: the consumers that
  // take a scoped_refptr<BandwidthThrottle> treat a null ref as "no
  // throttling", so pass them a null ref instead of a zero-rate throttle.
  //
  // `burst_duration` controls the maximum burst size:
  //     burst_size = throughput_bytes_per_sec * burst_duration.
  // Must be strictly positive.
  explicit BandwidthThrottle(
      uint64_t throughput_bytes_per_sec,
      base::TimeDelta burst_duration = base::Milliseconds(100));

  BandwidthThrottle(const BandwidthThrottle&) = delete;
  BandwidthThrottle& operator=(const BandwidthThrottle&) = delete;

  // Requests `bytes` from the throttle. `callback` is invoked (with no
  // arguments) when the requested bytes have been admitted by the token
  // bucket. Completion is always delivered on a separate task, so `callback`
  // never fires before RequestBytes returns — callers can safely chain it
  // with their own Read/Write contracts.
  //
  // The returned CancellationHandle auto-cancels the request when destroyed.
  // Callers that wrap a socket-bound request must hold the CancellationHandle
  // for the duration of the request; otherwise the request will be
  // cancelled (and the callback will never fire).
  //
  // `bytes` must be strictly positive. `callback` must not be null.
  [[nodiscard]] CancellationHandle RequestBytes(int bytes,
                                                ThrottleCallback callback);

  // Returns the configured throughput in bytes/sec.
  uint64_t throughput_bytes_per_sec() const;

 private:
  friend class base::RefCounted<BandwidthThrottle>;
  ~BandwidthThrottle();

  struct PendingRequest {
    PendingRequest(int bytes,
                   ThrottleCallback callback,
                   scoped_refptr<base::RefCountedData<bool>> canceled_flag);
    PendingRequest(PendingRequest&&);
    PendingRequest& operator=(PendingRequest&&);
    ~PendingRequest();

    int bytes;
    ThrottleCallback callback;
    // `canceled_flag` is shared with the consumer's CancellationHandle. Since
    // either one can outlive the other, it's a refptr to RefCountedData. When
    // true, the throttle skips this request without charging tokens.
    scoped_refptr<base::RefCountedData<bool>> canceled_flag;
  };

  void CheckValidSequence() const;

  // Refills tokens based on elapsed time since last refill.
  void RefillTokens();

  // Attempts to serve queued requests. Called after token refill or when a
  // new request arrives.
  void ProcessQueue();

  // Pops cancelled entries from the front of the queue. Cancelled entries
  // don't consume tokens, so we drop them before computing timing /
  // charging tokens for subsequent live requests.
  void DropCancelledFrontRequests();

  // Invoked by a CancellationHandle's Cancel() / destructor. Re-evaluates the
  // drain timer so a small successor isn't delayed by the wait that was
  // sized for the now-cancelled head.
  void OnCancellationHandleCancelled();

  // Schedules the drain timer for the next pending request.
  void ScheduleDrainTimer();

  // Timer callback for draining the queue.
  void OnDrainTimer();

  const uint64_t throughput_bytes_per_sec_;
  const double burst_size_;

  double available_tokens_;
  base::TimeTicks last_refill_time_;

  base::queue<PendingRequest> pending_requests_;
  base::OneShotTimer drain_timer_;

  // True while ProcessQueue() is iterating the queue. A re-entrant
  // RequestBytes() from inside a callback enqueues normally but skips
  // scheduling a fresh drain timer — ProcessQueue() will process the
  // newly-enqueued request itself if tokens allow, or schedule a timer
  // when it returns. Avoids posting a redundant zero-delay task.
  bool processing_queue_ = false;

  // Binds on first use and CHECKs that all later calls use the same sequence.
  // This protects the mutable token bucket, request queue, and timer state,
  // which are intentionally not locked. Used directly rather than via
  // SEQUENCE_CHECKER() so the check stays live in release: a cross-sequence
  // call here is a memory-safety bug, and the macro compiles to a no-op
  // outside DCHECK builds.
  mutable base::SequenceCheckerImpl sequence_checker_;

  base::WeakPtrFactory<BandwidthThrottle> weak_factory_{this};
};

}  // namespace net

#endif  // NET_SOCKET_BANDWIDTH_THROTTLE_H_
