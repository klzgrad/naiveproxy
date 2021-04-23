// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "base/power_monitor/power_monitor_device_source.h"

namespace base {

PowerMonitorDeviceSource::PowerMonitorDeviceSource() {
#if defined(OS_APPLE)
  PlatformInit();
#endif

#if defined(OS_WIN) || defined(OS_APPLE)
  // Provide the correct battery status if possible. Others platforms, such as
  // Android and ChromeOS, will update their status once their backends are
  // actually initialized.
  SetInitialOnBatteryPowerState(IsOnBatteryPowerImpl());
#endif
}

PowerMonitorDeviceSource::~PowerMonitorDeviceSource() {
#if defined(OS_APPLE)
  PlatformDestroy();
#endif
}

}  // namespace base
