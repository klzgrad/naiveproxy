// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "base/ios/scoped_critical_action.h"

#import <UIKit/UIKit.h>

#include "base/logging.h"
#include "base/memory/ref_counted.h"
#include "base/synchronization/lock.h"

namespace base {
namespace ios {

ScopedCriticalAction::ScopedCriticalAction()
    : core_(MakeRefCounted<ScopedCriticalAction::Core>()) {
  ScopedCriticalAction::Core::StartBackgroundTask(core_);
}

ScopedCriticalAction::~ScopedCriticalAction() {
  ScopedCriticalAction::Core::EndBackgroundTask(core_);
}

ScopedCriticalAction::Core::Core()
    : background_task_id_(UIBackgroundTaskInvalid) {}

ScopedCriticalAction::Core::~Core() {
  DCHECK_EQ(background_task_id_, UIBackgroundTaskInvalid);
}

// This implementation calls |beginBackgroundTaskWithExpirationHandler:| when
// instantiated and |endBackgroundTask:| when destroyed, creating a scope whose
// execution will continue (temporarily) even after the app is backgrounded.
// static
void ScopedCriticalAction::Core::StartBackgroundTask(scoped_refptr<Core> core) {
  UIApplication* application = [UIApplication sharedApplication];
  if (!application) {
    return;
  }

  core->background_task_id_ =
      [application beginBackgroundTaskWithExpirationHandler:^{
        DLOG(WARNING) << "Background task with id " << core->background_task_id_
                      << " expired.";
        // Note if |endBackgroundTask:| is not called for each task before time
        // expires, the system kills the application.
        EndBackgroundTask(core);
      }];

  if (core->background_task_id_ == UIBackgroundTaskInvalid) {
    DLOG(WARNING)
        << "beginBackgroundTaskWithExpirationHandler: returned an invalid ID";
  } else {
    VLOG(3) << "Beginning background task with id "
            << core->background_task_id_;
  }
}

// static
void ScopedCriticalAction::Core::EndBackgroundTask(scoped_refptr<Core> core) {
  UIBackgroundTaskIdentifier task_id;
  {
    AutoLock lock_scope(core->background_task_id_lock_);
    if (core->background_task_id_ == UIBackgroundTaskInvalid) {
      return;
    }
    task_id = core->background_task_id_;
    core->background_task_id_ = UIBackgroundTaskInvalid;
  }

  VLOG(3) << "Ending background task with id " << task_id;
  [[UIApplication sharedApplication] endBackgroundTask:task_id];
}

}  // namespace ios
}  // namespace base
