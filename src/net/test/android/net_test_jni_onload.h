// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef NET_TEST_ANDROID_NET_TEST_JNI_ONLOAD_H_
#define NET_TEST_ANDROID_NET_TEST_JNI_ONLOAD_H_

#include <jni.h>

namespace net {
namespace test {

bool OnJNIOnLoadInit();

}  // namespace test
}  // namespace net

#endif  // NET_TEST_ANDROID_NET_TEST_JNI_ONLOAD_H_
