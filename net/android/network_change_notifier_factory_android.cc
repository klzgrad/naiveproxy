// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/android/network_change_notifier_factory_android.h"

#include "net/android/network_change_notifier_android.h"
#include "net/android/network_change_notifier_delegate_android.h"

namespace net {

NetworkChangeNotifierFactoryAndroid::NetworkChangeNotifierFactoryAndroid() {}

NetworkChangeNotifierFactoryAndroid::~NetworkChangeNotifierFactoryAndroid() {}

NetworkChangeNotifier* NetworkChangeNotifierFactoryAndroid::CreateInstance() {
  return new NetworkChangeNotifierAndroid(&delegate_, nullptr);
}

}  // namespace net
