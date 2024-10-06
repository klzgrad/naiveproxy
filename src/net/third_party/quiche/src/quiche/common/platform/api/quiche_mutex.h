// Copyright (c) 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef QUICHE_COMMON_PLATFORM_API_QUICHE_MUTEX_H_
#define QUICHE_COMMON_PLATFORM_API_QUICHE_MUTEX_H_

#include "quiche_platform_impl/quiche_mutex_impl.h"

#define QUICHE_EXCLUSIVE_LOCKS_REQUIRED QUICHE_EXCLUSIVE_LOCKS_REQUIRED_IMPL
#define QUICHE_GUARDED_BY QUICHE_GUARDED_BY_IMPL
#define QUICHE_LOCKABLE QUICHE_LOCKABLE_IMPL
#define QUICHE_LOCKS_EXCLUDED QUICHE_LOCKS_EXCLUDED_IMPL
#define QUICHE_SHARED_LOCKS_REQUIRED QUICHE_SHARED_LOCKS_REQUIRED_IMPL
#define QUICHE_EXCLUSIVE_LOCK_FUNCTION QUICHE_EXCLUSIVE_LOCK_FUNCTION_IMPL
#define QUICHE_UNLOCK_FUNCTION QUICHE_UNLOCK_FUNCTION_IMPL
#define QUICHE_SHARED_LOCK_FUNCTION QUICHE_SHARED_LOCK_FUNCTION_IMPL
#define QUICHE_SCOPED_LOCKABLE QUICHE_SCOPED_LOCKABLE_IMPL
#define QUICHE_ASSERT_SHARED_LOCK QUICHE_ASSERT_SHARED_LOCK_IMPL

namespace quiche {

// A class representing a non-reentrant mutex in QUIC.
class QUICHE_LOCKABLE QUICHE_EXPORT QuicheMutex {
 public:
  QuicheMutex() = default;
  QuicheMutex(const QuicheMutex&) = delete;
  QuicheMutex& operator=(const QuicheMutex&) = delete;

  // Block until this Mutex is free, then acquire it exclusively.
  void WriterLock() QUICHE_EXCLUSIVE_LOCK_FUNCTION();

  // Release this Mutex. Caller must hold it exclusively.
  void WriterUnlock() QUICHE_UNLOCK_FUNCTION();

  // Block until this Mutex is free or shared, then acquire a share of it.
  void ReaderLock() QUICHE_SHARED_LOCK_FUNCTION();

  // Release this Mutex. Caller could hold it in shared mode.
  void ReaderUnlock() QUICHE_UNLOCK_FUNCTION();

  // Returns immediately if current thread holds the Mutex in at least shared
  // mode.  Otherwise, may report an error (typically by crashing with a
  // diagnostic), or may return immediately.
  void AssertReaderHeld() const QUICHE_ASSERT_SHARED_LOCK();

 private:
  QuicheLockImpl impl_;
};

// A helper class that acquires the given QuicheMutex shared lock while the
// QuicheReaderMutexLock is in scope.
class QUICHE_SCOPED_LOCKABLE QUICHE_EXPORT QuicheReaderMutexLock {
 public:
  explicit QuicheReaderMutexLock(QuicheMutex* lock)
      QUICHE_SHARED_LOCK_FUNCTION(lock);
  QuicheReaderMutexLock(const QuicheReaderMutexLock&) = delete;
  QuicheReaderMutexLock& operator=(const QuicheReaderMutexLock&) = delete;

  ~QuicheReaderMutexLock() QUICHE_UNLOCK_FUNCTION();

 private:
  QuicheMutex* const lock_;
};

// A helper class that acquires the given QuicheMutex exclusive lock while the
// QuicheWriterMutexLock is in scope.
class QUICHE_SCOPED_LOCKABLE QUICHE_EXPORT QuicheWriterMutexLock {
 public:
  explicit QuicheWriterMutexLock(QuicheMutex* lock)
      QUICHE_EXCLUSIVE_LOCK_FUNCTION(lock);
  QuicheWriterMutexLock(const QuicheWriterMutexLock&) = delete;
  QuicheWriterMutexLock& operator=(const QuicheWriterMutexLock&) = delete;

  ~QuicheWriterMutexLock() QUICHE_UNLOCK_FUNCTION();

 private:
  QuicheMutex* const lock_;
};

// A Notification allows threads to receive notification of a single occurrence
// of a single event.
class QUICHE_EXPORT QuicheNotification {
 public:
  QuicheNotification() = default;
  QuicheNotification(const QuicheNotification&) = delete;
  QuicheNotification& operator=(const QuicheNotification&) = delete;

  bool HasBeenNotified() { return impl_.HasBeenNotified(); }

  void Notify() { impl_.Notify(); }

  void WaitForNotification() { impl_.WaitForNotification(); }

 private:
  QuicheNotificationImpl impl_;
};

}  // namespace quiche

#endif  // QUICHE_COMMON_PLATFORM_API_QUICHE_MUTEX_H_
