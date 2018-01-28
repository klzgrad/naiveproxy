// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "base/message_loop/message_pump_android.h"

#include <jni.h>

#include "base/android/java_message_handler_factory.h"
#include "base/android/jni_android.h"
#include "base/android/scoped_java_ref.h"
#include "base/lazy_instance.h"
#include "base/logging.h"
#include "base/message_loop/message_loop.h"
#include "base/run_loop.h"
#include "base/time/time.h"
#include "jni/SystemMessageHandler_jni.h"

using base::android::JavaParamRef;
using base::android::ScopedJavaLocalRef;

// ----------------------------------------------------------------------------
// Native JNI methods called by Java.
// ----------------------------------------------------------------------------
// This method can not move to anonymous namespace as it has been declared as
// 'static' in system_message_handler_jni.h.
static void DoRunLoopOnce(JNIEnv* env,
                          const JavaParamRef<jobject>& obj,
                          jlong native_delegate,
                          jlong native_message_pump,
                          jlong delayed_scheduled_time_ticks) {
  base::MessagePump::Delegate* delegate =
      reinterpret_cast<base::MessagePump::Delegate*>(native_delegate);
  DCHECK(delegate);
  base::MessagePumpForUI* pump =
      reinterpret_cast<base::MessagePumpForUI*>(native_message_pump);
  DCHECK(pump);
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
  delegate->DoWork();
  if (pump->ShouldAbort()) {
    // There is a pending JNI exception, return to Java so that the exception is
    // thrown correctly.
    return;
  }

  // In the java side, |SystemMessageHandler| keeps a single "delayed" message.
  // It's an expensive operation to |removeMessage| there, so this is optimized
  // to avoid those calls.
  //
  // At this stage, |next_delayed_work_time| can be:
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
  //
  // Side note: base::TimeTicks is a C++ representation and can't be
  // compared in java. When calling |scheduleDelayedWork|, pass the
  // |InternalValue()| to java and then back to C++ so the comparisons can be
  // done here.
  // This roundtrip allows comparing TimeTicks directly (cheap) and
  // avoid comparisons with TimeDelta / Now() (expensive).
  base::TimeTicks next_delayed_work_time;
  delegate->DoDelayedWork(&next_delayed_work_time);
  if (pump->ShouldAbort()) {
    // There is a pending JNI exception, return to Java so that the exception is
    // thrown correctly
    return;
  }

  if (!next_delayed_work_time.is_null()) {
    // Schedule a new message if there's nothing already scheduled or there's a
    // shorter delay than previously scheduled (see (2) and (3) above).
    if (delayed_scheduled_time_ticks == 0 ||
        next_delayed_work_time < base::TimeTicks::FromInternalValue(
            delayed_scheduled_time_ticks)) {
      Java_SystemMessageHandler_scheduleDelayedWork(env, obj,
          next_delayed_work_time.ToInternalValue(),
          (next_delayed_work_time -
           base::TimeTicks::Now()).InMillisecondsRoundedUp());
    }
  }
}

// This is called by the java SystemMessageHandler whenever the message queue
// detects an idle state (as in, control returns to the looper and there are no
// tasks available to be run immediately).
// See the comments in DoRunLoopOnce for how this differs from the
// implementation on other platforms.
static void DoIdleWork(JNIEnv* env,
                       const JavaParamRef<jobject>& obj,
                       jlong native_delegate,
                       jlong native_message_pump) {
  base::MessagePump::Delegate* delegate =
      reinterpret_cast<base::MessagePump::Delegate*>(native_delegate);
  DCHECK(delegate);
  delegate->DoIdleWork();
};

namespace base {

MessagePumpForUI::MessagePumpForUI() = default;
MessagePumpForUI::~MessagePumpForUI() = default;

void MessagePumpForUI::Run(Delegate* delegate) {
  NOTREACHED() << "UnitTests should rely on MessagePumpForUIStub in"
      " test_stub_android.h";
}

JNIEnv* MessagePumpForUI::StartInternal() {
  DCHECK(!quit_);
  run_loop_ = new RunLoop();
  // Since the RunLoop was just created above, BeforeRun should be guaranteed to
  // return true (it only returns false if the RunLoop has been Quit already).
  if (!run_loop_->BeforeRun())
    NOTREACHED();

  DCHECK(system_message_handler_obj_.is_null());

  JNIEnv* env = base::android::AttachCurrentThread();
  DCHECK(env);
  return env;
}

void MessagePumpForUI::Start(Delegate* delegate) {
  JNIEnv* env = StartInternal();
  system_message_handler_obj_.Reset(Java_SystemMessageHandler_create(
      env, reinterpret_cast<intptr_t>(delegate),
      reinterpret_cast<intptr_t>(this)));
}

void MessagePumpForUI::StartForUnitTest(
    Delegate* delegate,
    base::android::JavaMessageHandlerFactory* factory,
    WaitableEvent* test_done_event) {
  JNIEnv* env = StartInternal();
  system_message_handler_obj_.Reset(
      factory->CreateMessageHandler(env, delegate, this, test_done_event));
}

void MessagePumpForUI::Quit() {
  quit_ = true;
  if (!system_message_handler_obj_.is_null()) {
    JNIEnv* env = base::android::AttachCurrentThread();
    DCHECK(env);

    Java_SystemMessageHandler_removeAllPendingMessages(
        env, system_message_handler_obj_);
    system_message_handler_obj_.Reset();
  }

  if (run_loop_) {
    run_loop_->AfterRun();
    delete run_loop_;
    run_loop_ = NULL;
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
  DCHECK(!system_message_handler_obj_.is_null());

  JNIEnv* env = base::android::AttachCurrentThread();
  DCHECK(env);

  jlong millis =
      (delayed_work_time - TimeTicks::Now()).InMillisecondsRoundedUp();
  // Note that we're truncating to milliseconds as required by the java side,
  // even though delayed_work_time is microseconds resolution.
  Java_SystemMessageHandler_scheduleDelayedWork(
      env, system_message_handler_obj_, delayed_work_time.ToInternalValue(),
      millis);
}

}  // namespace base
