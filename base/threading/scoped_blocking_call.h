// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef BASE_THREADING_SCOPED_BLOCKING_CALL_H
#define BASE_THREADING_SCOPED_BLOCKING_CALL_H

#include "base/base_export.h"
#include "base/logging.h"

namespace base {

// BlockingType indicates the likelihood that a blocking call will actually
// block.
enum class BlockingType {
  // The call might block (e.g. file I/O that might hit in memory cache).
  MAY_BLOCK,
  // The call will definitely block (e.g. cache already checked and now pinging
  // server synchronously).
  WILL_BLOCK
};

namespace internal {
class BlockingObserver;
}

// This class can be instantiated in a scope where a a blocking call (which
// isn't using local computing resources -- e.g. a synchronous network request)
// is made. Instantiation will hint the BlockingObserver for this thread about
// the scope of the blocking operation.
//
// In particular, when instantiated from a TaskScheduler parallel or sequenced
// task, this will allow the thread to be replaced in its pool (more or less
// aggressively depending on BlockingType).
class BASE_EXPORT ScopedBlockingCall {
 public:
  ScopedBlockingCall(BlockingType blocking_type);
  ~ScopedBlockingCall();

 private:
  internal::BlockingObserver* const blocking_observer_;

  // Previous ScopedBlockingCall instantiated on this thread.
  ScopedBlockingCall* const previous_scoped_blocking_call_;

  // Whether the BlockingType of the current thread was WILL_BLOCK after this
  // ScopedBlockingCall was instantiated.
  const bool is_will_block_;

  DISALLOW_COPY_AND_ASSIGN(ScopedBlockingCall);
};

namespace internal {

// Interface for an observer to be informed when a thread enters or exits
// the scope of ScopedBlockingCall objects.
class BASE_EXPORT BlockingObserver {
 public:
  virtual ~BlockingObserver() = default;

  // Invoked when a ScopedBlockingCall is instantiated on the observed thread
  // where there wasn't an existing ScopedBlockingCall.
  virtual void BlockingStarted(BlockingType blocking_type) = 0;

  // Invoked when a WILL_BLOCK ScopedBlockingCall is instantiated on the
  // observed thread where there was a MAY_BLOCK ScopedBlockingCall but not a
  // WILL_BLOCK ScopedBlockingCall.
  virtual void BlockingTypeUpgraded() = 0;

  // Invoked when the last ScopedBlockingCall on the observed thread is
  // destroyed.
  virtual void BlockingEnded() = 0;
};

// Registers |blocking_observer| on the current thread. It is invalid to call
// this on a thread where there is an active ScopedBlockingCall.
BASE_EXPORT void SetBlockingObserverForCurrentThread(
    BlockingObserver* blocking_observer);

BASE_EXPORT void ClearBlockingObserverForTesting();

// Unregisters the |blocking_observer| on the current thread within its scope.
// Used in TaskScheduler tests to prevent calls to //base sync primitives from
// affecting the thread pool capacity.
class BASE_EXPORT ScopedClearBlockingObserverForTesting {
 public:
  ScopedClearBlockingObserverForTesting();
  ~ScopedClearBlockingObserverForTesting();

 private:
  BlockingObserver* const blocking_observer_;

  DISALLOW_COPY_AND_ASSIGN(ScopedClearBlockingObserverForTesting);
};

}  // namespace internal

}  // namespace base

#endif  // BASE_THREADING_SCOPED_BLOCKING_CALL_H
