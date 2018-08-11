// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "base/android/java_handler_thread.h"

#include <jni.h>

#include "base/android/jni_android.h"
#include "base/android/jni_string.h"
#include "base/run_loop.h"
#include "base/synchronization/waitable_event.h"
#include "base/threading/thread_restrictions.h"
#include "jni/JavaHandlerThread_jni.h"

using base::android::AttachCurrentThread;

namespace base {

namespace android {

JavaHandlerThread::JavaHandlerThread(const char* name)
    : JavaHandlerThread(Java_JavaHandlerThread_create(
          AttachCurrentThread(),
          ConvertUTF8ToJavaString(AttachCurrentThread(), name))) {}

JavaHandlerThread::JavaHandlerThread(
    const base::android::ScopedJavaLocalRef<jobject>& obj)
    : java_thread_(obj) {}

JavaHandlerThread::~JavaHandlerThread() {
  JNIEnv* env = base::android::AttachCurrentThread();
  DCHECK(!Java_JavaHandlerThread_isAlive(env, java_thread_));
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
  base::ThreadRestrictions::ScopedAllowWait wait_allowed;
  initialize_event.Wait();
}

void JavaHandlerThread::Stop() {
  JNIEnv* env = base::android::AttachCurrentThread();
  Java_JavaHandlerThread_stop(env, java_thread_,
                              reinterpret_cast<intptr_t>(this));
}

void JavaHandlerThread::InitializeThread(JNIEnv* env,
                                         const JavaParamRef<jobject>& obj,
                                         jlong event) {
  // TYPE_JAVA to get the Android java style message loop.
  message_loop_ = new base::MessageLoop(base::MessageLoop::TYPE_JAVA);
  StartMessageLoop();
  reinterpret_cast<base::WaitableEvent*>(event)->Signal();
}

void JavaHandlerThread::StopThread(JNIEnv* env,
                                   const JavaParamRef<jobject>& obj) {
  StopMessageLoop();
}

void JavaHandlerThread::OnLooperStopped(JNIEnv* env,
                                        const JavaParamRef<jobject>& obj) {
  delete message_loop_;
  CleanUp();
}

void JavaHandlerThread::StartMessageLoop() {
  static_cast<MessageLoopForUI*>(message_loop_)->Start();
  Init();
}

void JavaHandlerThread::StopMessageLoop() {
  base::RunLoop::QuitCurrentWhenIdleDeprecated();
}

void JavaHandlerThread::StopMessageLoopForTesting() {
  JNIEnv* env = base::android::AttachCurrentThread();
  Java_JavaHandlerThread_stopOnThread(env, java_thread_,
                                      reinterpret_cast<intptr_t>(this));
}

void JavaHandlerThread::JoinForTesting() {
  JNIEnv* env = base::android::AttachCurrentThread();
  Java_JavaHandlerThread_joinThread(env, java_thread_);
}

void JavaHandlerThread::ListenForUncaughtExceptionsForTesting() {
  JNIEnv* env = base::android::AttachCurrentThread();
  Java_JavaHandlerThread_listenForUncaughtExceptionsForTesting(env,
                                                               java_thread_);
}

ScopedJavaLocalRef<jthrowable> JavaHandlerThread::GetUncaughtExceptionIfAny() {
  JNIEnv* env = base::android::AttachCurrentThread();
  return Java_JavaHandlerThread_getUncaughtExceptionIfAny(env, java_thread_);
}

} // namespace android
} // namespace base
