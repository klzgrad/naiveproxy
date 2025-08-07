// Copyright 2012 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "base/system/system_monitor.h"

#include <utility>

#include "base/logging.h"
#include "base/time/time.h"

namespace base {

static SystemMonitor* g_system_monitor = nullptr;

SystemMonitor::SystemMonitor()
    : devices_changed_observer_list_(
          new ObserverListThreadSafe<DevicesChangedObserver>()) {
  DCHECK(!g_system_monitor);
  g_system_monitor = this;
}

SystemMonitor::~SystemMonitor() {
  DCHECK_EQ(this, g_system_monitor);
  g_system_monitor = nullptr;
}

// static
SystemMonitor* SystemMonitor::Get() {
  return g_system_monitor;
}

void SystemMonitor::ProcessDevicesChanged(DeviceType device_type) {
  NotifyDevicesChanged(device_type);
}

void SystemMonitor::AddDevicesChangedObserver(DevicesChangedObserver* obs) {
  devices_changed_observer_list_->AddObserver(obs);
}

void SystemMonitor::RemoveDevicesChangedObserver(DevicesChangedObserver* obs) {
  devices_changed_observer_list_->RemoveObserver(obs);
}

void SystemMonitor::NotifyDevicesChanged(DeviceType device_type) {
  DVLOG(1) << "DevicesChanged with device type " << device_type;
  devices_changed_observer_list_->Notify(
      FROM_HERE, &DevicesChangedObserver::OnDevicesChanged, device_type);
}

}  // namespace base
