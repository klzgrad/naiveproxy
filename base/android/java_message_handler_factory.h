// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef BASE_ANDROID_JAVA_MESSAGE_HANDLER_FACTORY_H_
#define BASE_ANDROID_JAVA_MESSAGE_HANDLER_FACTORY_H_

#include "base/android/scoped_java_ref.h"
#include "base/message_loop/message_pump.h"

namespace base {

class MessagePumpForUI;
class WaitableEvent;

namespace android {

// Factory for creating the Java-side system message handler - only used for
// testing.
class JavaMessageHandlerFactory {
 public:
  virtual ~JavaMessageHandlerFactory() {}
  virtual base::android::ScopedJavaLocalRef<jobject> CreateMessageHandler(
      JNIEnv* env,
      base::MessagePump::Delegate* delegate,
      MessagePumpForUI* message_pump,
      WaitableEvent* test_done_event) = 0;
};

}  // namespace android
}  // namespace base

#endif  // BASE_ANDROID_JAVA_MESSAGE_HANDLER_FACTORY_H_
