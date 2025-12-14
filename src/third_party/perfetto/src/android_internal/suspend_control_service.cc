/*
 * Copyright (C) 2025 The Android Open Source Project
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

#include "src/android_internal/suspend_control_service.h"

#include <string.h>

#include <memory>
#include <vector>

#include <android/system/suspend/internal/ISuspendControlServiceInternal.h>

#include <binder/IServiceManager.h>

namespace perfetto {
namespace android_internal {

namespace aidl = android::system::suspend::internal;

static android::sp<aidl::ISuspendControlServiceInternal> svc_ = nullptr;

aidl::ISuspendControlServiceInternal* MaybeGetService() {
  if (svc_ == nullptr) {
    svc_ = android::waitForService<aidl::ISuspendControlServiceInternal>(
        android::String16("suspend_control_internal"));
  }
  return svc_.get();
}

void ResetService() {
  svc_.clear();
}

bool GetKernelWakelocks(KernelWakelock* wakelock, size_t* size_of_arr) {
  const size_t in_array_size = *size_of_arr;
  *size_of_arr = 0;

  aidl::ISuspendControlServiceInternal* svc = MaybeGetService();
  if (svc == nullptr) {
    return false;
  }

  std::vector<aidl::WakeLockInfo> results;

  android::binder::Status status = svc->getWakeLockStatsFiltered(
      aidl::ISuspendControlServiceInternal::WAKE_LOCK_INFO_TOTAL_TIME |
          aidl::ISuspendControlServiceInternal::
              WAKE_LOCK_INFO_IS_KERNEL_WAKELOCK,
      &results);

  if (!status.isOk()) {
    if (status.transactionError() == android::DEAD_OBJECT) {
      auto& cur = wakelock[(*size_of_arr)++];
      strlcpy(cur.wakelock_name, "dead", sizeof(cur.wakelock_name));
      // Service has died.  Reset it to attempt to acquire a new one next time.
      ResetService();
    }
    return false;
  }

  size_t max_size = std::min(in_array_size, results.size());
  for (const auto& result : results) {
    if (*size_of_arr >= max_size) {
      break;
    }
    auto& cur = wakelock[(*size_of_arr)++];
    strlcpy(cur.wakelock_name, result.name.c_str(), sizeof(cur.wakelock_name));
    cur.total_time_ms = result.totalTime;
    cur.is_kernel = result.isKernelWakelock;
  }
  return true;
}

}  // namespace android_internal
}  // namespace perfetto
