// Copyright (c) 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef NET_QUIC_PLATFORM_IMPL_QUIC_MUTEX_IMPL_H_
#define NET_QUIC_PLATFORM_IMPL_QUIC_MUTEX_IMPL_H_

#include "base/macros.h"
#include "base/synchronization/lock.h"
#include "net/quic/platform/api/quic_export.h"

#ifndef EXCLUSIVE_LOCK_FUNCTION
#define EXCLUSIVE_LOCK_FUNCTION(...)
#endif

#ifndef UNLOCK_FUNCTION
#define UNLOCK_FUNCTION(...)
#endif

#ifndef SHARED_LOCK_FUNCTION
#define SHARED_LOCK_FUNCTION(...)
#endif

#ifndef ASSERT_SHARED_LOCK
#define ASSERT_SHARED_LOCK(...)
#endif

#ifndef LOCKABLE
#define LOCKABLE
#endif

#ifndef SCOPED_LOCKABLE
#define SCOPED_LOCKABLE
#endif

#ifndef GUARDED_BY
#define GUARDED_BY(x)
#endif

#ifndef SHARED_LOCKS_REQUIRED
#define SHARED_LOCKS_REQUIRED(...)
#endif

namespace net {

// A class wrapping a non-reentrant mutex.
class QUIC_EXPORT_PRIVATE QuicLockImpl {
 public:
  QuicLockImpl() = default;

  // Block until lock_ is free, then acquire it exclusively.
  void WriterLock() EXCLUSIVE_LOCK_FUNCTION();

  // Release lock_. Caller must hold it exclusively.
  void WriterUnlock() UNLOCK_FUNCTION();

  // Block until lock_ is free or shared, then acquire a share of it.
  void ReaderLock() SHARED_LOCK_FUNCTION();

  // Release lock_. Caller could hold it in shared mode.
  void ReaderUnlock() UNLOCK_FUNCTION();

  // Not implemented.
  void AssertReaderHeld() const ASSERT_SHARED_LOCK() {}

 private:
  base::Lock lock_;

  DISALLOW_COPY_AND_ASSIGN(QuicLockImpl);
};

}  // namespace net

#endif  // NET_QUIC_PLATFORM_IMPL_QUIC_MUTEX_IMPL_H_
