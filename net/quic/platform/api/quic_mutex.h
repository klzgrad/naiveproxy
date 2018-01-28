// Copyright (c) 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef NET_QUIC_PLATFORM_API_QUIC_MUTEX_H_
#define NET_QUIC_PLATFORM_API_QUIC_MUTEX_H_

#include "base/macros.h"
#include "net/quic/platform/impl/quic_mutex_impl.h"

namespace net {

// A class representing a non-reentrant mutex in QUIC.
class QUIC_EXPORT_PRIVATE LOCKABLE QuicMutex {
 public:
  QuicMutex() = default;

  // Block until this Mutex is free, then acquire it exclusively.
  void WriterLock() EXCLUSIVE_LOCK_FUNCTION();

  // Release this Mutex. Caller must hold it exclusively.
  void WriterUnlock() UNLOCK_FUNCTION();

  // Block until this Mutex is free or shared, then acquire a share of it.
  void ReaderLock() SHARED_LOCK_FUNCTION();

  // Release this Mutex. Caller could hold it in shared mode.
  void ReaderUnlock() UNLOCK_FUNCTION();

  // Returns immediately if current thread holds the Mutex in at least shared
  // mode.  Otherwise, may report an error (typically by crashing with a
  // diagnostic), or may return immediately.
  void AssertReaderHeld() const ASSERT_SHARED_LOCK();

 private:
  QuicLockImpl impl_;

  DISALLOW_COPY_AND_ASSIGN(QuicMutex);
};

// A helper class that acquires the given QuicMutex shared lock while the
// QuicReaderMutexLock is in scope.
class QUIC_EXPORT_PRIVATE SCOPED_LOCKABLE QuicReaderMutexLock {
 public:
  explicit QuicReaderMutexLock(QuicMutex* lock) SHARED_LOCK_FUNCTION(lock);

  ~QuicReaderMutexLock() UNLOCK_FUNCTION();

 private:
  QuicMutex* const lock_;

  DISALLOW_COPY_AND_ASSIGN(QuicReaderMutexLock);
};

// A helper class that acquires the given QuicMutex exclusive lock while the
// QuicWriterMutexLock is in scope.
class QUIC_EXPORT_PRIVATE SCOPED_LOCKABLE QuicWriterMutexLock {
 public:
  explicit QuicWriterMutexLock(QuicMutex* lock) EXCLUSIVE_LOCK_FUNCTION(lock);

  ~QuicWriterMutexLock() UNLOCK_FUNCTION();

 private:
  QuicMutex* const lock_;

  DISALLOW_COPY_AND_ASSIGN(QuicWriterMutexLock);
};

}  // namespace net

#endif  // NET_QUIC_PLATFORM_API_QUIC_MUTEX_H_
