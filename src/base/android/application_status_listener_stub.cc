// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "base/android/application_status_listener.h"

namespace base {
namespace android {

ApplicationStatusListener::ApplicationStatusListener() = default;
ApplicationStatusListener::~ApplicationStatusListener() = default;

// static
std::unique_ptr<ApplicationStatusListener> ApplicationStatusListener::New(
    const ApplicationStateChangeCallback& callback) {
  return nullptr;
}

// static
void ApplicationStatusListener::NotifyApplicationStateChange(
    ApplicationState state) {
}

// static
ApplicationState ApplicationStatusListener::GetState() {
  return {};
}

// static
bool ApplicationStatusListener::HasVisibleActivities() {
  return false;
}

}  // namespace android
}  // namespace base
