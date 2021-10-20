#ifndef QUICHE_COMMON_PLATFORM_DEFAULT_QUICHE_PLATFORM_IMPL_QUIC_MUTEX_IMPL_H_
#define QUICHE_COMMON_PLATFORM_DEFAULT_QUICHE_PLATFORM_IMPL_QUIC_MUTEX_IMPL_H_

#include "absl/synchronization/mutex.h"
#include "absl/synchronization/notification.h"
#include "quic/platform/api/quic_export.h"

#define QUIC_EXCLUSIVE_LOCKS_REQUIRED_IMPL ABSL_EXCLUSIVE_LOCKS_REQUIRED
#define QUIC_GUARDED_BY_IMPL ABSL_GUARDED_BY
#define QUIC_LOCKABLE_IMPL ABSL_LOCKABLE
#define QUIC_LOCKS_EXCLUDED_IMPL ABSL_LOCKS_EXCLUDED
#define QUIC_SHARED_LOCKS_REQUIRED_IMPL ABSL_SHARED_LOCKS_REQUIRED
#define QUIC_EXCLUSIVE_LOCK_FUNCTION_IMPL ABSL_EXCLUSIVE_LOCK_FUNCTION
#define QUIC_UNLOCK_FUNCTION_IMPL ABSL_UNLOCK_FUNCTION
#define QUIC_SHARED_LOCK_FUNCTION_IMPL ABSL_SHARED_LOCK_FUNCTION
#define QUIC_SCOPED_LOCKABLE_IMPL ABSL_SCOPED_LOCKABLE
#define QUIC_ASSERT_SHARED_LOCK_IMPL ABSL_ASSERT_SHARED_LOCK

namespace quic {

// A class wrapping a non-reentrant mutex.
class ABSL_LOCKABLE QUIC_EXPORT_PRIVATE QuicLockImpl {
 public:
  QuicLockImpl() = default;
  QuicLockImpl(const QuicLockImpl&) = delete;
  QuicLockImpl& operator=(const QuicLockImpl&) = delete;

  // Block until mu_ is free, then acquire it exclusively.
  void WriterLock() ABSL_EXCLUSIVE_LOCK_FUNCTION();

  // Release mu_. Caller must hold it exclusively.
  void WriterUnlock() ABSL_UNLOCK_FUNCTION();

  // Block until mu_ is free or shared, then acquire a share of it.
  void ReaderLock() ABSL_SHARED_LOCK_FUNCTION();

  // Release mu_. Caller could hold it in shared mode.
  void ReaderUnlock() ABSL_UNLOCK_FUNCTION();

  // Returns immediately if current thread holds mu_ in at least shared
  // mode.  Otherwise, reports an error by crashing with a diagnostic.
  void AssertReaderHeld() const ABSL_ASSERT_SHARED_LOCK();

 private:
  absl::Mutex mu_;
};

// A Notification allows threads to receive notification of a single occurrence
// of a single event.
class QUIC_EXPORT_PRIVATE QuicNotificationImpl {
 public:
  QuicNotificationImpl() = default;
  QuicNotificationImpl(const QuicNotificationImpl&) = delete;
  QuicNotificationImpl& operator=(const QuicNotificationImpl&) = delete;

  bool HasBeenNotified() { return notification_.HasBeenNotified(); }

  void Notify() { notification_.Notify(); }

  void WaitForNotification() { notification_.WaitForNotification(); }

 private:
  absl::Notification notification_;
};

}  // namespace quic

#endif  // QUICHE_COMMON_PLATFORM_DEFAULT_QUICHE_PLATFORM_IMPL_QUIC_MUTEX_IMPL_H_
