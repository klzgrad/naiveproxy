// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef BASE_MESSAGE_LOOP_MESSAGE_PUMP_ANDROID_H_
#define BASE_MESSAGE_LOOP_MESSAGE_PUMP_ANDROID_H_

#include <jni.h>
#include <memory>

#include "base/android/scoped_java_ref.h"
#include "base/base_export.h"
#include "base/compiler_specific.h"
#include "base/macros.h"
#include "base/message_loop/message_pump.h"
#include "base/time/time.h"

namespace base {

class RunLoop;

// This class implements a MessagePump needed for TYPE_UI MessageLoops on
// OS_ANDROID platform.
class BASE_EXPORT MessagePumpForUI : public MessagePump {
 public:
  MessagePumpForUI();
  ~MessagePumpForUI() override;

  void DoIdleWork(JNIEnv* env, const base::android::JavaParamRef<jobject>& obj);
  void DoRunLoopOnce(JNIEnv* env,
                     const base::android::JavaParamRef<jobject>& obj,
                     jboolean delayed);

  void Run(Delegate* delegate) override;
  void Quit() override;
  void ScheduleWork() override;
  void ScheduleDelayedWork(const TimeTicks& delayed_work_time) override;

  virtual void Start(Delegate* delegate);

  // We call Abort when there is a pending JNI exception, meaning that the
  // current thread will crash when we return to Java.
  // We can't call any JNI-methods before returning to Java as we would then
  // cause a native crash (instead of the original Java crash).
  void Abort() { should_abort_ = true; }
  bool ShouldAbort() const { return should_abort_; }

 private:
  std::unique_ptr<RunLoop> run_loop_;
  base::android::ScopedJavaGlobalRef<jobject> system_message_handler_obj_;
  bool should_abort_ = false;
  bool quit_ = false;
  Delegate* delegate_ = nullptr;
  base::TimeTicks delayed_scheduled_time_;

  DISALLOW_COPY_AND_ASSIGN(MessagePumpForUI);
};

}  // namespace base

#endif  // BASE_MESSAGE_LOOP_MESSAGE_PUMP_ANDROID_H_
