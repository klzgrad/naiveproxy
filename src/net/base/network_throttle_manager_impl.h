// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef NET_BASE_NETWORK_THROTTLE_MANAGER_IMPL_H_
#define NET_BASE_NETWORK_THROTTLE_MANAGER_IMPL_H_

#include <list>
#include <memory>
#include <set>

#include "base/memory/weak_ptr.h"
#include "base/time/tick_clock.h"
#include "base/time/time.h"
#include "base/timer/timer.h"
#include "net/base/network_throttle_manager.h"
#include "net/base/percentile_estimator.h"

namespace net {

// The NetworkThrottleManagerImpl implements the following semantics:
// * All throttles of priority above THROTTLED are created unblocked.
// * Throttles of priority THROTTLED are created unblocked, unless
//   there are |kActiveRequestThrottlingLimit| or more throttles active,
//   in which case they are created blocked.
//   When that condition is no longer true, throttles of priority
//   THROTTLED are unblocked, in FIFO order.
// * Throttles that have been alive for more than |kMedianLifetimeMultiple|
//   times the current estimate of the throttle median lifetime do
//   not count against the |kActiveRequestThrottlingLimit| limit.
class NET_EXPORT NetworkThrottleManagerImpl : public NetworkThrottleManager {
 public:
  // Maximum number of active requests before new THROTTLED throttles
  // are created blocked.  Throttles are unblocked as the active requests
  // fall below this limit.
  static const size_t kActiveRequestThrottlingLimit;

  // Note that the following constants are implementation details exposed in the
  // header file only for testing, and should not be relied on by consumers.

  // Constants used for the running estimate of the median lifetime
  // for throttles created by this class.  That estimate is used to detect
  // throttles that are "unusually old" and hence may represent hanging GETs
  // or long-running streams.  Such throttles should not be considered
  // "active" for the purposes of determining whether THROTTLED throttles
  // should be created in a blocked state.
  // Note that the precise details of this algorithm aren't very important;
  // specifically, if it takes a while for the median estimate to reach the
  // "actual" median of a request stream, the consequence is either a bit more
  // of a delay in unblocking THROTTLED requests or more THROTTLED requests
  // being unblocked than would be ideal (i.e. performance tweaks at
  // the margins).

  // Multiple of the current median lifetime beyond which a throttle is
  // considered "unusually old" and not considered in counting active
  // requests. This is used instead of a percentile estimate because the goal
  // is eliminating requests that are qualitatively different
  // (e.g. hanging gets, streams), and the percentage of all requests
  // that are in that category can vary greatly.
  static const int kMedianLifetimeMultiple;

  // The median lifetime estimate starts at class creation at
  // |kInitialMedianInMs|.
  static const int kInitialMedianInMs;

  NetworkThrottleManagerImpl();
  ~NetworkThrottleManagerImpl() override;

  // NetworkThrottleManager:
  std::unique_ptr<Throttle> CreateThrottle(ThrottleDelegate* delegate,
                                           RequestPriority priority,
                                           bool ignore_limits) override;

  void SetTickClockForTesting(const base::TickClock* tick_clock);

  // If the |NowTicks()| value of |tick_clock_| is greater than the
  // time the outstanding_recomputation_timer_ has set to go off, Stop()
  // the timer and manually run the associated user task.  This is to allow
  // "fast-forwarding" of the clock for testing by working around
  // base::Timer's direct use of base::TimeTicks rather than a base::TickClock.
  //
  // Note specifically that base::Timer::Start takes a time delta into the
  // future and adds it to base::TimeTicks::Now() to get
  // base::Timer::desired_run_time(), which is what this method compares
  // |tick_clock_->NowTicks()| against.  So tests should be written so that
  // the timer Start() routine whose callback should be run is called
  // with |tick_clock_| in accord with wallclock time.  This routine can then
  // be called with |tick_clock_| set into the future.
  //
  // Returns true if there was a timer running and it was triggerred
  // (|tick_clock_->NowTicks() >
  //   outstanding_recomputation_timer_.desired_run_time()|).
  bool ConditionallyTriggerTimerForTesting();

 private:
  class ThrottleImpl;
  using ThrottleList = std::list<ThrottleImpl*>;

  void OnThrottlePriorityChanged(ThrottleImpl* throttle,
                                 RequestPriority old_priority,
                                 RequestPriority new_priority);
  void OnThrottleDestroyed(ThrottleImpl* throttle);

  // Recompute how many requests count as outstanding (i.e.
  // are not older than kMedianLifetimeMultiple * MedianThrottleLifetime()).
  // If outstanding_recomputation_timer_ is not set, it will be set
  // to the earliest a throttle might "age out" of the outstanding list.
  void RecomputeOutstanding();

  // Unblock the specified throttle.  May result in re-entrant calls
  // into NetworkThrottleManagerImpl.
  void UnblockThrottle(ThrottleImpl* throttle);

  // Recomputes how many requests count as outstanding, checks to see
  // if any currently blocked throttles should be unblocked,
  // and unblock them if so.  Note that unblocking may result in
  // re-entrant calls to this class, so no assumptions about state persistence
  // should be made across this call.
  void MaybeUnblockThrottles();

  PercentileEstimator lifetime_median_estimate_;

  // base::Timer controlling outstanding request recomputation.
  //
  // This is started whenever it is not running and a new throttle is
  // added to |outstanding_throttles_|, and is never cleared except by
  // execution, which re-starts it if there are any
  // outstanding_throttles_.  So it should always be running if any
  // throttles are outstanding.  This guarantees that the class will
  // eventually detect aging out of outstanding throttles and unblock
  // throttles blocked on those outstanding throttles.
  std::unique_ptr<base::OneShotTimer> outstanding_recomputation_timer_;

  // FIFO of OUTSTANDING throttles (ordered by time of entry into the
  // OUTSTANDING state).
  ThrottleList outstanding_throttles_;

  // FIFO list of BLOCKED throttles.  This is a list so that the
  // throttles can store iterators to themselves.
  ThrottleList blocked_throttles_;

  // For testing.
  const base::TickClock* tick_clock_;

  base::WeakPtrFactory<NetworkThrottleManagerImpl> weak_ptr_factory_;

  DISALLOW_COPY_AND_ASSIGN(NetworkThrottleManagerImpl);
};

}  // namespace net

#endif  // NET_BASE_NETWORK_THROTTLE_MANAGER_IMPL_H_
