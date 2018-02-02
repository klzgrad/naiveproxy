// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef NET_DNS_SERIAL_WORKER_H_
#define NET_DNS_SERIAL_WORKER_H_

#include <string>

#include "base/compiler_specific.h"
#include "base/macros.h"
#include "base/memory/ref_counted.h"
#include "base/sequence_checker.h"
#include "base/task_scheduler/task_traits.h"
#include "net/base/net_export.h"

namespace net {

// SerialWorker executes a job on TaskScheduler serially -- **once at a time**.
// On |WorkNow|, a call to |DoWork| is scheduled on TaskScheduler. Once it
// completes, |OnWorkFinished| is called on the origin thread. If |WorkNow| is
// called (1 or more times) while |DoWork| is already under way, |DoWork| will
// be called once: after current |DoWork| completes, before a call to
// |OnWorkFinished|.
//
// This behavior is designed for updating a result after some trigger, for
// example reading a file once FilePathWatcher indicates it changed.
//
// Derived classes should store results of work done in |DoWork| in dedicated
// fields and read them in |OnWorkFinished| which is executed on the origin
// thread. This avoids the need to template this class.
//
// This implementation avoids locking by using the |state_| member to ensure
// that |DoWork| and |OnWorkFinished| cannot execute in parallel.
class NET_EXPORT_PRIVATE SerialWorker
    : public base::RefCountedThreadSafe<SerialWorker> {
 public:
  SerialWorker();

  // Unless already scheduled, post |DoWork| to TaskScheduler.
  // Made virtual to allow mocking.
  virtual void WorkNow();

  // Stop scheduling jobs.
  void Cancel();

  bool IsCancelled() const { return state_ == CANCELLED; }

 protected:
  friend class base::RefCountedThreadSafe<SerialWorker>;
  // protected to allow sub-classing, but prevent deleting
  virtual ~SerialWorker();

  // Executed on TaskScheduler, at most once at a time.
  virtual void DoWork() = 0;

  // Executed on origin thread after |DoRead| completes.
  virtual void OnWorkFinished() = 0;

  // Used to verify that the constructor, WorkNow(), Cancel() and
  // OnWorkJobFinished() are called on the same sequence.
  SEQUENCE_CHECKER(sequence_checker_);

 private:
  enum State {
    CANCELLED = -1,
    IDLE = 0,
    WORKING,  // |DoWorkJob| posted to TaskScheduler, until |OnWorkJobFinished|
    PENDING,  // |WorkNow| while WORKING, must re-do work
  };

  // Called on the the origin thread after |DoWork| completes.
  void OnWorkJobFinished();

  State state_;

  DISALLOW_COPY_AND_ASSIGN(SerialWorker);
};

}  // namespace net

#endif  // NET_DNS_SERIAL_WORKER_H_
