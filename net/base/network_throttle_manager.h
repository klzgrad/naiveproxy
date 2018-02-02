// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef NET_BASE_NETWORK_THROTTLE_MANAGER_H_
#define NET_BASE_NETWORK_THROTTLE_MANAGER_H_

#include <memory>

#include "base/memory/weak_ptr.h"
#include "net/base/net_export.h"
#include "net/base/request_priority.h"

namespace net {

// This class controls throttling based on priority level and number of
// outstanding requests.  It vends Throttle objects, and tracks
// outstanding requests by the lifetime of those objects.  Consumers
// determine whether or not they are throttled by consulting those
// Throttle objects.
//
// This class must outlive all Throttles created from it via CreateThrottle().
//
// Methods are virtual to allow for test mocks.
class NET_EXPORT_PRIVATE NetworkThrottleManager {
 public:
  class Throttle;

  // Abstract base class other classes can inherit from to get
  // notifications from throttle state changes.
  class NET_EXPORT_PRIVATE ThrottleDelegate {
   public:
    // Called when a throttle is unblocked.
    //
    // Note that this call may occur as the result of either a call to
    // Throttle::SetPriority (on the throttle related to this delegate
    // or another throttle) or the destruction of a Throttle, and if
    // so will occur synchronously during those events.  It will not
    // be called from the destructor of the Throttle associated with
    // the ThrottleDelegate.
    virtual void OnThrottleUnblocked(Throttle* throttle) = 0;

   protected:
    virtual ~ThrottleDelegate() {}
  };

  // Class owned by external stream representations that
  // routes notifications.  It may be constructed in either the
  // blocked or unblocked state according to the state of the
  // NetworkThrottleManager; if it's constructed in the unblocked
  // state, it will only make a single transition to unblocked,
  // which will be signaled by delegate->OnThrottleUnblocked(this).
  // If it's constructed in the unblocked state, it will remain
  // there.
  class NET_EXPORT_PRIVATE Throttle {
   public:
    virtual ~Throttle() {}

    virtual bool IsBlocked() const = 0;

    virtual RequestPriority Priority() const = 0;

    // Note that this may result in a possibly reentrant call to
    // |ThrottleDelegate::OnThrottleUnblocked|, as well as the resumption
    // of this or other requests, which may result in request completion
    // and destruction before return.  Any caller of this function
    // should not rely on this object or containing objects surviving
    // this call.
    //
    // This call is a no-op if the priority is set to its current value.
    virtual void SetPriority(RequestPriority priority) = 0;

   protected:
    Throttle() {}

   private:
    DISALLOW_COPY_AND_ASSIGN(Throttle);
  };

  virtual ~NetworkThrottleManager() {}

  // Caller must ensure that |*delegate| outlives the returned
  // Throttle.
  virtual std::unique_ptr<Throttle> CreateThrottle(ThrottleDelegate* delegate,
                                                   RequestPriority priority,
                                                   bool ignore_limits) = 0;

 protected:
  NetworkThrottleManager() {}

 private:
  DISALLOW_COPY_AND_ASSIGN(NetworkThrottleManager);
};

}  // namespace net

#endif  // NET_BASE_NETWORK_THROTTLE_MANAGER_H_
