// Copyright (c) 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef QUICHE_QUIC_PLATFORM_API_QUIC_MUTEX_H_
#define QUICHE_QUIC_PLATFORM_API_QUIC_MUTEX_H_

#include "net/quic/platform/impl/quic_mutex_impl.h"

#define QUIC_EXCLUSIVE_LOCKS_REQUIRED QUIC_EXCLUSIVE_LOCKS_REQUIRED_IMPL
#define QUIC_GUARDED_BY QUIC_GUARDED_BY_IMPL
#define QUIC_LOCKABLE QUIC_LOCKABLE_IMPL
#define QUIC_LOCKS_EXCLUDED QUIC_LOCKS_EXCLUDED_IMPL
#define QUIC_SHARED_LOCKS_REQUIRED QUIC_SHARED_LOCKS_REQUIRED_IMPL
#define QUIC_EXCLUSIVE_LOCK_FUNCTION QUIC_EXCLUSIVE_LOCK_FUNCTION_IMPL
#define QUIC_UNLOCK_FUNCTION QUIC_UNLOCK_FUNCTION_IMPL
#define QUIC_SHARED_LOCK_FUNCTION QUIC_SHARED_LOCK_FUNCTION_IMPL
#define QUIC_SCOPED_LOCKABLE QUIC_SCOPED_LOCKABLE_IMPL
#define QUIC_ASSERT_SHARED_LOCK QUIC_ASSERT_SHARED_LOCK_IMPL

namespace quic {

// A class representing a non-reentrant mutex in QUIC.
class QUIC_LOCKABLE QUIC_EXPORT_PRIVATE QuicMutex {
 public:
  QuicMutex() = default;
  QuicMutex(const QuicMutex&) = delete;
  QuicMutex& operator=(const QuicMutex&) = delete;

  // Block until this Mutex is free, then acquire it exclusively.
  void WriterLock() QUIC_EXCLUSIVE_LOCK_FUNCTION();

  // Release this Mutex. Caller must hold it exclusively.
  void WriterUnlock() QUIC_UNLOCK_FUNCTION();

  // Block until this Mutex is free or shared, then acquire a share of it.
  void ReaderLock() QUIC_SHARED_LOCK_FUNCTION();

  // Release this Mutex. Caller could hold it in shared mode.
  void ReaderUnlock() QUIC_UNLOCK_FUNCTION();

  // Returns immediately if current thread holds the Mutex in at least shared
  // mode.  Otherwise, may report an error (typically by crashing with a
  // diagnostic), or may return immediately.
  void AssertReaderHeld() const QUIC_ASSERT_SHARED_LOCK();

 private:
  QuicLockImpl impl_;
};

// A helper class that acquires the given QuicMutex shared lock while the
// QuicReaderMutexLock is in scope.
class QUIC_SCOPED_LOCKABLE QUIC_EXPORT_PRIVATE QuicReaderMutexLock {
 public:
  explicit QuicReaderMutexLock(QuicMutex* lock) QUIC_SHARED_LOCK_FUNCTION(lock);
  QuicReaderMutexLock(const QuicReaderMutexLock&) = delete;
  QuicReaderMutexLock& operator=(const QuicReaderMutexLock&) = delete;

  ~QuicReaderMutexLock() QUIC_UNLOCK_FUNCTION();

 private:
  QuicMutex* const lock_;
};

// A helper class that acquires the given QuicMutex exclusive lock while the
// QuicWriterMutexLock is in scope.
class QUIC_SCOPED_LOCKABLE QUIC_EXPORT_PRIVATE QuicWriterMutexLock {
 public:
  explicit QuicWriterMutexLock(QuicMutex* lock)
      QUIC_EXCLUSIVE_LOCK_FUNCTION(lock);
  QuicWriterMutexLock(const QuicWriterMutexLock&) = delete;
  QuicWriterMutexLock& operator=(const QuicWriterMutexLock&) = delete;

  ~QuicWriterMutexLock() QUIC_UNLOCK_FUNCTION();

 private:
  QuicMutex* const lock_;
};

// A Notification allows threads to receive notification of a single occurrence
// of a single event.
class QUIC_EXPORT_PRIVATE QuicNotification {
 public:
  QuicNotification() = default;
  QuicNotification(const QuicNotification&) = delete;
  QuicNotification& operator=(const QuicNotification&) = delete;

  bool HasBeenNotified() { return impl_.HasBeenNotified(); }

  void Notify() { impl_.Notify(); }

  void WaitForNotification() { impl_.WaitForNotification(); }

 private:
  QuicNotificationImpl impl_;
};

}  // namespace quic

#endif  // QUICHE_QUIC_PLATFORM_API_QUIC_MUTEX_H_
