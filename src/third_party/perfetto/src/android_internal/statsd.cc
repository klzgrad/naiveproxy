/*
 * Copyright (C) 2023 The Android Open Source Project
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

#include "statsd.h"

#include <binder/ProcessState.h>
#include <stats_subscription.h>

namespace perfetto {
namespace android_internal {

int32_t AddAtomSubscription(const uint8_t* subscription_config,
                            size_t num_bytes,
                            const AtomCallback callback,
                            void* cookie) {
  // Although the binder messages we use are one-way we pass an interface that
  // statsd uses to talk back to us. For this to work we need the some binder
  // threads listening to the for these messages. To handle this we start a
  // thread pool if it hasn't been started already:
  android::ProcessState::self()->startThreadPool();

  auto c = reinterpret_cast<AStatsManager_SubscriptionCallback>(callback);
  return AStatsManager_addSubscription(subscription_config, num_bytes, c,
                                       cookie);
}

void RemoveAtomSubscription(int32_t subscription_id) {
  AStatsManager_removeSubscription(subscription_id);
}

void FlushAtomSubscription(int32_t subscription_id) {
  AStatsManager_flushSubscription(subscription_id);
}

}  // namespace android_internal
}  // namespace perfetto
