// Copyright 2021 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/android/radio_activity_tracker.h"

#include "net/traffic_annotation/network_traffic_annotation.h"

namespace net {
namespace android {

void MaybeRecordTCPWriteForWakeupTrigger(
    const NetworkTrafficAnnotationTag& traffic_annotation) {}

void MaybeRecordUDPWriteForWakeupTrigger(
    const NetworkTrafficAnnotationTag& traffic_annotation) {}

}  // namespace android
}  // namespace net
