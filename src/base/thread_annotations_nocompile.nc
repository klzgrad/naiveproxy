// Copyright 2018 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// This is a "No Compile Test" suite.
// https://dev.chromium.org/developers/testing/no-compile-tests

#include "base/thread_annotations.h"

namespace {

class LOCKABLE Lock {
 public:
  void Acquire() EXCLUSIVE_LOCK_FUNCTION() {}
  void Release() UNLOCK_FUNCTION() {}
};

class SCOPED_LOCKABLE AutoLock {
 public:
  AutoLock(Lock& lock) EXCLUSIVE_LOCK_FUNCTION(lock) : lock_(lock) {
    lock.Acquire();
  }
  ~AutoLock() UNLOCK_FUNCTION() { lock_.Release(); }

 private:
  Lock& lock_;
};

class ThreadSafe {
 public:
  void IncrementWithoutRelease();
  void IncrementWithoutAcquire();
  void IncrementWithWronglyScopedLock();
 private:
  Lock lock_;
  int counter_ GUARDED_BY(lock_);
};

void ThreadSafe::IncrementWithoutRelease() {
  lock_.Acquire();
  ++counter_;
  // Forgot to release the lock.
}  // expected-error {{mutex 'lock_' is still held at the end of function}}

void ThreadSafe::IncrementWithoutAcquire() {
  // Member access without holding the lock guarding it.
  ++counter_;  // expected-error {{writing variable 'counter_' requires holding mutex 'lock_' exclusively}}
}

void ThreadSafe::IncrementWithWronglyScopedLock() {
  {
    AutoLock auto_lock(lock_);
    // The AutoLock will go out of scope before the guarded member access.
  }
  ++counter_;  // expected-error {{writing variable 'counter_' requires holding mutex 'lock_' exclusively}}
}

int not_lockable;
int global_counter GUARDED_BY(not_lockable);  // expected-error {{'guarded_by' attribute requires arguments whose type is annotated with 'capability' attribute}}

}  // anonymous namespace
