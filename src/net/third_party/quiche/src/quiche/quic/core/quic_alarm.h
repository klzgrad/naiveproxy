// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef QUICHE_QUIC_CORE_QUIC_ALARM_H_
#define QUICHE_QUIC_CORE_QUIC_ALARM_H_

#include "quiche/quic/core/quic_arena_scoped_ptr.h"
#include "quiche/quic/core/quic_connection_context.h"
#include "quiche/quic/core/quic_time.h"
#include "quiche/quic/platform/api/quic_export.h"

namespace quic {

// Abstract class which represents an alarm which will go off at a
// scheduled time, and execute the |OnAlarm| method of the delegate.
// An alarm may be cancelled, in which case it may or may not be
// removed from the underlying scheduling system, but in either case
// the task will not be executed.
class QUIC_EXPORT_PRIVATE QuicAlarm {
 public:
  class QUIC_EXPORT_PRIVATE Delegate {
   public:
    virtual ~Delegate() {}

    // If the alarm belongs to a single QuicConnection, return the corresponding
    // QuicConnection.context_. Note the context_ is the first member of
    // QuicConnection, so it should outlive the delegate.
    // Otherwise return nullptr.
    // The OnAlarm function will be called under the connection context, if any.
    virtual QuicConnectionContext* GetConnectionContext() = 0;

    // Invoked when the alarm fires.
    virtual void OnAlarm() = 0;
  };

  // DelegateWithContext is a Delegate with a QuicConnectionContext* stored as a
  // member variable.
  class QUIC_EXPORT_PRIVATE DelegateWithContext : public Delegate {
   public:
    explicit DelegateWithContext(QuicConnectionContext* context)
        : context_(context) {}
    ~DelegateWithContext() override {}
    QuicConnectionContext* GetConnectionContext() override { return context_; }

   private:
    QuicConnectionContext* context_;
  };

  // DelegateWithoutContext is a Delegate that does not have a corresponding
  // context. Typically this means one object of the child class deals with many
  // connections.
  class QUIC_EXPORT_PRIVATE DelegateWithoutContext : public Delegate {
   public:
    ~DelegateWithoutContext() override {}
    QuicConnectionContext* GetConnectionContext() override { return nullptr; }
  };

  explicit QuicAlarm(QuicArenaScopedPtr<Delegate> delegate);
  QuicAlarm(const QuicAlarm&) = delete;
  QuicAlarm& operator=(const QuicAlarm&) = delete;
  virtual ~QuicAlarm();

  // Sets the alarm to fire at |deadline|.  Must not be called while
  // the alarm is set.  To reschedule an alarm, call Cancel() first,
  // then Set().
  void Set(QuicTime new_deadline);

  // Both PermanentCancel() and Cancel() can cancel the alarm. If permanent,
  // future calls to Set() and Update() will become no-op except emitting an
  // error log.
  //
  // Both may be called repeatedly.  Does not guarantee that the underlying
  // scheduling system will remove the alarm's associated task, but guarantees
  // that the delegates OnAlarm method will not be called.
  void PermanentCancel() { CancelInternal(true); }
  void Cancel() { CancelInternal(false); }

  // Return true if PermanentCancel() has been called.
  bool IsPermanentlyCancelled() const;

  // Cancels and sets the alarm if the |deadline| is farther from the current
  // deadline than |granularity|, and otherwise does nothing.  If |deadline| is
  // not initialized, the alarm is cancelled.
  void Update(QuicTime new_deadline, QuicTime::Delta granularity);

  // Returns true if |deadline_| has been set to a non-zero time.
  bool IsSet() const;

  QuicTime deadline() const { return deadline_; }

 protected:
  // Subclasses implement this method to perform the platform-specific
  // scheduling of the alarm.  Is called from Set() or Fire(), after the
  // deadline has been updated.
  virtual void SetImpl() = 0;

  // Subclasses implement this method to perform the platform-specific
  // cancelation of the alarm.
  virtual void CancelImpl() = 0;

  // Subclasses implement this method to perform the platform-specific update of
  // the alarm if there exists a more optimal implementation than calling
  // CancelImpl() and SetImpl().
  virtual void UpdateImpl();

  // Called by subclasses when the alarm fires.  Invokes the
  // delegates |OnAlarm| if a delegate is set, and if the deadline
  // has been exceeded.  Implementations which do not remove the
  // alarm from the underlying scheduler on Cancel() may need to handle
  // the situation where the task executes before the deadline has been
  // reached, in which case they need to reschedule the task and must not
  // call invoke this method.
  void Fire();

 private:
  void CancelInternal(bool permanent);

  QuicArenaScopedPtr<Delegate> delegate_;
  QuicTime deadline_;
};

}  // namespace quic

#endif  // QUICHE_QUIC_CORE_QUIC_ALARM_H_
