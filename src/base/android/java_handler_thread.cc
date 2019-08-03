// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "base/android/java_handler_thread.h"

#include <jni.h>

#include "base/android/jni_android.h"
#include "base/android/jni_string.h"
#include "base/bind.h"
#include "base/run_loop.h"
#include "base/synchronization/waitable_event.h"
#include "base/threading/platform_thread_internal_posix.h"
#include "base/threading/thread_id_name_manager.h"
#include "base/threading/thread_restrictions.h"
#include "jni/JavaHandlerThread_jni.h"

using base::android::AttachCurrentThread;

namespace base {

namespace android {

JavaHandlerThread::JavaHandlerThread(const char* name,
                                     base::ThreadPriority priority)
    : JavaHandlerThread(
          name,
          Java_JavaHandlerThread_create(
              AttachCurrentThread(),
              ConvertUTF8ToJavaString(AttachCurrentThread(), name),
              base::internal::ThreadPriorityToNiceValue(priority))) {}

JavaHandlerThread::JavaHandlerThread(
    const char* name,
    const base::android::ScopedJavaLocalRef<jobject>& obj)
    : name_(name), java_thread_(obj) {}

JavaHandlerThread::~JavaHandlerThread() {
  JNIEnv* env = base::android::AttachCurrentThread();
  DCHECK(!Java_JavaHandlerThread_isAlive(env, java_thread_));
  DCHECK(!message_loop_ || message_loop_->IsAborted());
  // TODO(mthiesse): We shouldn't leak the MessageLoop as this could affect
  // future tests.
  if (message_loop_ && message_loop_->IsAborted()) {
    // When the message loop has been aborted due to a crash, we intentionally
    // leak the message loop because the message loop hasn't been shut down
    // properly and would trigger DCHECKS. This should only happen in tests,
    // where we handle the exception instead of letting it take down the
    // process.
    message_loop_.release();
  }
}

void JavaHandlerThread::Start() {
  // Check the thread has not already been started.
  DCHECK(!message_loop_);

  JNIEnv* env = base::android::AttachCurrentThread();
  base::WaitableEvent initialize_event(
      WaitableEvent::ResetPolicy::AUTOMATIC,
      WaitableEvent::InitialState::NOT_SIGNALED);
  Java_JavaHandlerThread_startAndInitialize(
      env, java_thread_, reinterpret_cast<intptr_t>(this),
      reinterpret_cast<intptr_t>(&initialize_event));
  // Wait for thread to be initialized so it is ready to be used when Start
  // returns.
  base::ScopedAllowBaseSyncPrimitivesOutsideBlockingScope wait_allowed;
  initialize_event.Wait();
}

void JavaHandlerThread::Stop() {
  DCHECK(!task_runner()->BelongsToCurrentThread());
  task_runner()->PostTask(
      FROM_HERE,
      base::BindOnce(&JavaHandlerThread::StopOnThread, base::Unretained(this)));
  JNIEnv* env = base::android::AttachCurrentThread();
  Java_JavaHandlerThread_joinThread(env, java_thread_);
}

void JavaHandlerThread::InitializeThread(JNIEnv* env,
                                         const JavaParamRef<jobject>& obj,
                                         jlong event) {
  base::ThreadIdNameManager::GetInstance()->RegisterThread(
      base::PlatformThread::CurrentHandle().platform_handle(),
      base::PlatformThread::CurrentId());

  if (name_)
    PlatformThread::SetName(name_);

  // TYPE_JAVA to get the Android java style message loop.
  message_loop_ =
      std::make_unique<MessageLoopForUI>(base::MessageLoop::TYPE_JAVA);
  Init();
  reinterpret_cast<base::WaitableEvent*>(event)->Signal();
}

void JavaHandlerThread::OnLooperStopped(JNIEnv* env,
                                        const JavaParamRef<jobject>& obj) {
  DCHECK(task_runner()->BelongsToCurrentThread());
  message_loop_.reset();

  CleanUp();

  base::ThreadIdNameManager::GetInstance()->RemoveName(
      base::PlatformThread::CurrentHandle().platform_handle(),
      base::PlatformThread::CurrentId());
}

void JavaHandlerThread::StopMessageLoopForTesting() {
  DCHECK(task_runner()->BelongsToCurrentThread());
  StopOnThread();
}

void JavaHandlerThread::JoinForTesting() {
  DCHECK(!task_runner()->BelongsToCurrentThread());
  JNIEnv* env = base::android::AttachCurrentThread();
  Java_JavaHandlerThread_joinThread(env, java_thread_);
}

void JavaHandlerThread::ListenForUncaughtExceptionsForTesting() {
  DCHECK(!task_runner()->BelongsToCurrentThread());
  JNIEnv* env = base::android::AttachCurrentThread();
  Java_JavaHandlerThread_listenForUncaughtExceptionsForTesting(env,
                                                               java_thread_);
}

ScopedJavaLocalRef<jthrowable> JavaHandlerThread::GetUncaughtExceptionIfAny() {
  DCHECK(!task_runner()->BelongsToCurrentThread());
  JNIEnv* env = base::android::AttachCurrentThread();
  return Java_JavaHandlerThread_getUncaughtExceptionIfAny(env, java_thread_);
}

void JavaHandlerThread::StopOnThread() {
  DCHECK(task_runner()->BelongsToCurrentThread());
  message_loop_->QuitWhenIdle(base::BindOnce(
      &JavaHandlerThread::QuitThreadSafely, base::Unretained(this)));
}

void JavaHandlerThread::QuitThreadSafely() {
  DCHECK(task_runner()->BelongsToCurrentThread());
  JNIEnv* env = base::android::AttachCurrentThread();
  Java_JavaHandlerThread_quitThreadSafely(env, java_thread_,
                                          reinterpret_cast<intptr_t>(this));
}

} // namespace android
} // namespace base
