// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef BASE_TEST_POWER_MONITOR_TEST_BASE_H_
#define BASE_TEST_POWER_MONITOR_TEST_BASE_H_

#include "base/power_monitor/power_monitor.h"
#include "base/power_monitor/power_monitor_source.h"

namespace base {

// Use PowerMonitorTestSource via ScopedPowerMonitorTestSource wrapper when you
// need to simulate power events (suspend and resume).
class PowerMonitorTestSource;

namespace test {

// ScopedPowerMonitorTestSource initializes the PowerMonitor with a Mock
// PowerMonitorSource. Mock power notifications can be simulated through this
// helper class.
//
// Example:
//   base::test::ScopedPowerMonitorTestSource power_monitor_source;
//   power_monitor_source.Suspend();
//   [...]
//   power_monitor_source.Resume();
class ScopedPowerMonitorTestSource {
 public:
  ScopedPowerMonitorTestSource();
  ~ScopedPowerMonitorTestSource();

  ScopedPowerMonitorTestSource(const ScopedPowerMonitorTestSource&) = delete;
  ScopedPowerMonitorTestSource& operator=(const ScopedPowerMonitorTestSource&) =
      delete;

  // Sends asynchronous notifications to registered observers.
  void Suspend();
  void Resume();
  void SetOnBatteryPower(bool on_battery_power);

  void GeneratePowerStateEvent(bool on_battery_power);

 private:
  // Owned by PowerMonitor.
  PowerMonitorTestSource* power_monitor_test_source_ = nullptr;
};

}  // namespace test

// TODO(crbug/1188692): Move `PowerMonitorTestSource` and
// `PowerMonitorTestObserver` into `test` namespace.
class PowerMonitorTestSource : public PowerMonitorSource {
 public:
  PowerMonitorTestSource();
  ~PowerMonitorTestSource() override;

  // Retrieve current states.
  PowerThermalObserver::DeviceThermalState GetCurrentThermalState() override;
  bool IsOnBatteryPower() override;

  // Sends asynchronous notifications to registered observers.
  void Suspend();
  void Resume();
  void SetOnBatteryPower(bool on_battery_power);

  // Sends asynchronous notifications to registered observers and ensures they
  // are executed (i.e. RunUntilIdle()).
  void GeneratePowerStateEvent(bool on_battery_power);
  void GenerateSuspendEvent();
  void GenerateResumeEvent();
  void GenerateThermalThrottlingEvent(
      PowerThermalObserver::DeviceThermalState new_thermal_state);

 protected:
  bool test_on_battery_power_ = false;
  PowerThermalObserver::DeviceThermalState current_thermal_state_ =
      PowerThermalObserver::DeviceThermalState::kUnknown;
};

class PowerMonitorTestObserver : public PowerSuspendObserver,
                                 public PowerThermalObserver,
                                 public PowerStateObserver {
 public:
  PowerMonitorTestObserver();
  ~PowerMonitorTestObserver() override;

  // PowerStateObserver overrides.
  void OnPowerStateChange(bool on_battery_power) override;
  // PowerSuspendObserver overrides.
  void OnSuspend() override;
  void OnResume() override;
  // PowerThermalObserver overrides.
  void OnThermalStateChange(
      PowerThermalObserver::DeviceThermalState new_state) override;

  // Test status counts.
  int power_state_changes() const { return power_state_changes_; }
  int suspends() const { return suspends_; }
  int resumes() const { return resumes_; }
  int thermal_state_changes() const { return thermal_state_changes_; }

  bool last_power_state() const { return last_power_state_; }
  PowerThermalObserver::DeviceThermalState last_thermal_state() const {
    return last_thermal_state_;
  }

 private:
  // Count of OnPowerStateChange notifications.
  int power_state_changes_ = 0;
  // Count of OnSuspend notifications.
  int suspends_ = 0;
  // Count of OnResume notifications.
  int resumes_ = 0;
  // Count of OnThermalStateChange notifications.
  int thermal_state_changes_ = 0;

  // Last power state we were notified of.
  bool last_power_state_ = false;
  // Last power thermal we were notified of.
  PowerThermalObserver::DeviceThermalState last_thermal_state_ =
      PowerThermalObserver::DeviceThermalState::kUnknown;
};

}  // namespace base

#endif  // BASE_TEST_POWER_MONITOR_TEST_BASE_H_
