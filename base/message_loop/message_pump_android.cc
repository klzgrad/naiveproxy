// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "base/message_loop/message_pump_android.h"

#include <jni.h>

#include "base/android/jni_android.h"
#include "base/android/scoped_java_ref.h"
#include "base/lazy_instance.h"
#include "base/logging.h"
#include "base/message_loop/message_loop.h"
#include "base/run_loop.h"
#include "jni/SystemMessageHandler_jni.h"

using base::android::JavaParamRef;
using base::android::ScopedJavaLocalRef;

namespace base {

MessagePumpForUI::MessagePumpForUI() = default;
MessagePumpForUI::~MessagePumpForUI() = default;

// This is called by the java SystemMessageHandler whenever the message queue
// detects an idle state (as in, control returns to the looper and there are no
// tasks available to be run immediately).
// See the comments in DoRunLoopOnce for how this differs from the
// implementation on other platforms.
void MessagePumpForUI::DoIdleWork(JNIEnv* env,
                                  const JavaParamRef<jobject>& obj) {
  delegate_->DoIdleWork();
}

void MessagePumpForUI::DoRunLoopOnce(JNIEnv* env,
                                     const JavaParamRef<jobject>& obj,
                                     jboolean delayed) {
  if (delayed)
    delayed_scheduled_time_ = base::TimeTicks();

  // If the pump has been aborted, tasks may continue to be queued up, but
  // shouldn't run.
  if (ShouldAbort())
    return;

  // This is based on MessagePumpForUI::DoRunLoop() from desktop.
  // Note however that our system queue is handled in the java side.
  // In desktop we inspect and process a single system message and then
  // we call DoWork() / DoDelayedWork(). This is then wrapped in a for loop and
  // repeated until no work is left to do, at which point DoIdleWork is called.
  // On Android, the java message queue may contain messages for other handlers
  // that will be processed before calling here again.
  // This means that unlike Desktop, we can't wrap a for loop around this
  // function and keep processing tasks until we have no work left to do - we
  // have to return control back to the Android Looper after each message. This
  // also means we have to perform idle detection differently, which is why we
  // add an IdleHandler to the message queue in SystemMessageHandler.java, which
  // calls DoIdleWork whenever control returns back to the looper and there are
  // no tasks queued up to run immediately.
  delegate_->DoWork();
  if (ShouldAbort()) {
    // There is a pending JNI exception, return to Java so that the exception is
    // thrown correctly.
    return;
  }

  base::TimeTicks next_delayed_work_time;
  delegate_->DoDelayedWork(&next_delayed_work_time);
  if (ShouldAbort()) {
    // There is a pending JNI exception, return to Java so that the exception is
    // thrown correctly
    return;
  }

  if (!next_delayed_work_time.is_null())
    ScheduleDelayedWork(next_delayed_work_time);
}

void MessagePumpForUI::Run(Delegate* delegate) {
  NOTREACHED() << "UnitTests should rely on MessagePumpForUIStub in"
                  " test_stub_android.h";
}

void MessagePumpForUI::Start(Delegate* delegate) {
  DCHECK(!quit_);
  delegate_ = delegate;
  run_loop_ = std::make_unique<RunLoop>();
  // Since the RunLoop was just created above, BeforeRun should be guaranteed to
  // return true (it only returns false if the RunLoop has been Quit already).
  if (!run_loop_->BeforeRun())
    NOTREACHED();

  DCHECK(system_message_handler_obj_.is_null());

  JNIEnv* env = base::android::AttachCurrentThread();
  DCHECK(env);
  system_message_handler_obj_.Reset(
      Java_SystemMessageHandler_create(env, reinterpret_cast<jlong>(this)));
}

void MessagePumpForUI::Quit() {
  quit_ = true;

  if (!system_message_handler_obj_.is_null()) {
    JNIEnv* env = base::android::AttachCurrentThread();
    DCHECK(env);

    Java_SystemMessageHandler_shutdown(env, system_message_handler_obj_);
    system_message_handler_obj_.Reset();
  }

  if (run_loop_) {
    run_loop_->AfterRun();
    run_loop_ = nullptr;
  }
}

void MessagePumpForUI::ScheduleWork() {
  if (quit_)
    return;
  DCHECK(!system_message_handler_obj_.is_null());

  JNIEnv* env = base::android::AttachCurrentThread();
  DCHECK(env);

  Java_SystemMessageHandler_scheduleWork(env, system_message_handler_obj_);
}

void MessagePumpForUI::ScheduleDelayedWork(const TimeTicks& delayed_work_time) {
  if (quit_)
    return;
  // In the java side, |SystemMessageHandler| keeps a single "delayed" message.
  // It's an expensive operation to |removeMessage| there, so this is optimized
  // to avoid those calls.
  //
  // At this stage, |delayed_work_time| can be:
  // 1) The same as previously scheduled: nothing to be done, move along. This
  // is the typical case, since this method is called for every single message.
  //
  // 2) Not previously scheduled: just post a new message in java.
  //
  // 3) Shorter than previously scheduled: far less common. In this case,
  // |removeMessage| and post a new one.
  //
  // 4) Longer than previously scheduled (or null): nothing to be done, move
  // along.
  if (!delayed_scheduled_time_.is_null() &&
      delayed_work_time >= delayed_scheduled_time_) {
    return;
  }
  DCHECK(!delayed_work_time.is_null());
  DCHECK(!system_message_handler_obj_.is_null());

  JNIEnv* env = base::android::AttachCurrentThread();
  DCHECK(env);

  jlong millis =
      (delayed_work_time - TimeTicks::Now()).InMillisecondsRoundedUp();
  delayed_scheduled_time_ = delayed_work_time;
  // Note that we're truncating to milliseconds as required by the java side,
  // even though delayed_work_time is microseconds resolution.
  Java_SystemMessageHandler_scheduleDelayedWork(
      env, system_message_handler_obj_, millis);
}

}  // namespace base
