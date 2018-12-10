// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "jni/X509Util_jni.h"
#include "net/cert/cert_database.h"

using base::android::JavaParamRef;

namespace net {

void JNI_X509Util_NotifyKeyChainChanged(JNIEnv* env,
                                        const JavaParamRef<jclass>& clazz) {
  CertDatabase::GetInstance()->NotifyObserversCertDBChanged();
}

}  // namespace net
