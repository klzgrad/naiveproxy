/*
 * Copyright (C) 2018 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "src/android_internal/health_hal.h"

#include <aidl/android/hardware/health/IHealth.h>
#include <android/binder_manager.h>
#include <android/hardware/health/2.0/IHealth.h>
#include <healthhalutils/HealthHalUtils.h>

namespace perfetto {
namespace android_internal {

using HidlHealth = ::android::hardware::health::V2_0::IHealth;
using ::aidl::android::hardware::health::IHealth;
using ::android::hardware::Return;
using ::android::hardware::health::V2_0::Result;

namespace {

struct HealthService {
  android::sp<HidlHealth> hidl;
  std::shared_ptr<IHealth> aidl;
};

HealthService g_svc;

void ResetService() {
  auto aidl_name = std::string(IHealth::descriptor) + "/default";
  if (AServiceManager_isDeclared(aidl_name.c_str())) {
    ndk::SpAIBinder binder(AServiceManager_waitForService(aidl_name.c_str()));
    g_svc.aidl = IHealth::fromBinder(binder);
    if (g_svc.aidl != nullptr) {
      return;
    }
  }
  g_svc.hidl = ::android::hardware::health::V2_0::get_health_service();
}

bool GetBatteryCounterHidl(BatteryCounter counter, int64_t* value) {
  // The Android HIDL documentation states that for blocking services, the
  // caller blocks until the reply is received and the callback is called inline
  // in the same thread.
  // See https://source.android.com/devices/architecture/hidl/threading .

  Return<void> ret;
  Result res = Result::UNKNOWN;
  switch (counter) {
    case BatteryCounter::kUnspecified:
      break;

    case BatteryCounter::kCharge:
      ret = g_svc.hidl->getChargeCounter(
          [&res, value](Result hal_res, int32_t hal_value) {
            res = hal_res;
            *value = hal_value;
          });
      break;

    case BatteryCounter::kCapacityPercent:
      ret = g_svc.hidl->getCapacity(
          [&res, value](Result hal_res, int32_t hal_value) {
            res = hal_res;
            *value = hal_value;
          });
      break;

    case BatteryCounter::kCurrent:
      ret = g_svc.hidl->getCurrentNow(
          [&res, value](Result hal_res, int32_t hal_value) {
            res = hal_res;
            *value = hal_value;
          });
      break;

    case BatteryCounter::kCurrentAvg:
      ret = g_svc.hidl->getCurrentAverage(
          [&res, value](Result hal_res, int32_t hal_value) {
            res = hal_res;
            *value = hal_value;
          });
      break;

    case BatteryCounter::kVoltage:
      g_svc.hidl->getHealthInfo(
          [&res, value](Result hal_res, const auto& hal_health_info) {
            res = hal_res;
            // batteryVoltage is in mV, convert to uV.
            *value = hal_health_info.legacy.batteryVoltage * 1000;
          });
      break;
  }  // switch(counter)

  if (ret.isDeadObject())
    g_svc.hidl.clear();

  return ret.isOk() && res == Result::SUCCESS;
}

bool GetBatteryCounterAidl(BatteryCounter counter, int64_t* value) {
  ndk::ScopedAStatus status;
  int32_t value32;

  switch (counter) {
    case BatteryCounter::kUnspecified:
      return false;

    case BatteryCounter::kCharge:
      status = g_svc.aidl->getChargeCounterUah(&value32);
      break;

    case BatteryCounter::kCapacityPercent:
      status = g_svc.aidl->getCapacity(&value32);
      break;

    case BatteryCounter::kCurrent:
      status = g_svc.aidl->getCurrentNowMicroamps(&value32);
      break;

    case BatteryCounter::kCurrentAvg:
      status = g_svc.aidl->getCurrentAverageMicroamps(&value32);
      break;

    case BatteryCounter::kVoltage:
      ::aidl::android::hardware::health::HealthInfo health_info;
      status = g_svc.aidl->getHealthInfo(&health_info);
      // Convert from mV to uV.
      value32 = health_info.batteryVoltageMillivolts * 1000;
      break;
  }  // switch(counter)

  if (status.isOk()) {
    *value = value32;
    return true;
  }

  if (status.getStatus() == STATUS_DEAD_OBJECT)
    g_svc.aidl.reset();

  return false;
}

}  // namespace

bool GetBatteryCounter(BatteryCounter counter, int64_t* value) {
  *value = 0;
  if (!g_svc.aidl && !g_svc.hidl)
    ResetService();

  if (!g_svc.aidl && !g_svc.hidl)
    return false;

  if (g_svc.aidl)
    return GetBatteryCounterAidl(counter, value);

  return GetBatteryCounterHidl(counter, value);
}

}  // namespace android_internal
}  // namespace perfetto
