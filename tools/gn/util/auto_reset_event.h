// Copyright 2018 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can
// be found in the LICENSE file.

#ifndef UTIL_AUTO_RESET_EVENT_H_
#define UTIL_AUTO_RESET_EVENT_H_

#include <atomic>

#include "base/logging.h"
#include "util/semaphore.h"

// From http://preshing.com/20150316/semaphores-are-surprisingly-versatile/,
// but using V8's Semaphore.
class AutoResetEvent {
 private:
  // status_ == 1: Event object is signaled.
  // status_ == 0: Event object is reset and no threads are waiting.
  // status_ == -N: Event object is reset and N threads are waiting.
  std::atomic<int> status_;
  Semaphore semaphore_;

 public:
  AutoResetEvent() : status_(0), semaphore_(0) {}

  void Signal() {
    int old_status = status_.load(std::memory_order_relaxed);
    // Increment status_ atomically via CAS loop.
    for (;;) {
      DCHECK_LE(old_status, 1);
      int new_status = old_status < 1 ? old_status + 1 : 1;
      if (status_.compare_exchange_weak(old_status, new_status,
                                        std::memory_order_release,
                                        std::memory_order_relaxed)) {
        break;
      }
      // The compare-exchange failed, likely because another thread changed
      // status_. old_status has been updated. Retry the CAS loop.
    }
    if (old_status < 0)
      semaphore_.Signal();  // Release one waiting thread.
  }

  void Wait() {
    int old_status = status_.fetch_sub(1, std::memory_order_acquire);
    DCHECK_LE(old_status, 1);
    if (old_status < 1) {
      semaphore_.Wait();
    }
  }
};

#endif  // UTIL_AUTO_RESET_EVENT_H_
