// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef BASE_MEMORY_MEMORY_COORDINATOR_CLIENT_H_
#define BASE_MEMORY_MEMORY_COORDINATOR_CLIENT_H_

#include "base/base_export.h"

namespace base {

// OVERVIEW:
//
// MemoryCoordinatorClient is an interface which a component can implement to
// adjust "future allocation" and "existing allocation". For "future allocation"
// it provides a callback to observe memory state changes, and for "existing
// allocation" it provides a callback to purge memory.
//
// Unlike MemoryPressureListener, memory state changes are stateful. State
// transitions are throttled to avoid thrashing; the exact throttling period is
// platform dependent, but will be at least 5-10 seconds. When a state change
// notification is dispatched, clients are expected to update their allocation
// policies (e.g. setting cache limit) that persist for the duration of the
// memory state. Note that clients aren't expected to free up memory on memory
// state changes. Clients should wait for a separate purge request to free up
// memory. Purging requests will be throttled as well.

// MemoryState is an indicator that processes can use to guide their memory
// allocation policies. For example, a process that receives the throttled
// state can use that as as signal to decrease memory cache limits.
// NOTE: This enum is used to back an UMA histogram, and therefore should be
// treated as append-only.
enum class MemoryState : int {
  // The state is unknown.
  UNKNOWN = -1,
  // No memory constraints.
  NORMAL = 0,
  // Running and interactive but memory allocation should be throttled.
  // Clients should set lower budget for any memory that is used as an
  // optimization but that is not necessary for the process to run.
  // (e.g. caches)
  THROTTLED = 1,
  // Still resident in memory but core processing logic has been suspended.
  // In most cases, OnPurgeMemory() will be called before entering this state.
  SUSPENDED = 2,
};

const int kMemoryStateMax = static_cast<int>(MemoryState::SUSPENDED) + 1;

// Returns a string representation of MemoryState.
BASE_EXPORT const char* MemoryStateToString(MemoryState state);

// This is an interface for components which can respond to memory status
// changes. An initial state is NORMAL. See MemoryCoordinatorClientRegistry for
// threading guarantees and ownership management.
class BASE_EXPORT MemoryCoordinatorClient {
 public:
  // Called when memory state has changed. Any transition can occur except for
  // UNKNOWN. General guidelines are:
  //  * NORMAL:    Restore the default settings for memory allocation/usage if
  //               it has changed.
  //  * THROTTLED: Use smaller limits for future memory allocations. You don't
  //               need to take any action on existing allocations.
  //  * SUSPENDED: Use much smaller limits for future memory allocations. You
  //               don't need to take any action on existing allocations.
  virtual void OnMemoryStateChange(MemoryState state) {}

  // Called to purge memory.
  // This callback should free up any memory that is used as an optimization, or
  // any memory whose contents can be reproduced.
  virtual void OnPurgeMemory() {}

 protected:
  virtual ~MemoryCoordinatorClient() = default;
};

}  // namespace base

#endif  // BASE_MEMORY_MEMORY_COORDINATOR_CLIENT_H_
