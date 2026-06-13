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

#ifndef INCLUDE_PERFETTO_EXT_TRACING_CORE_PRIORITY_BOOST_CONFIG_H_
#define INCLUDE_PERFETTO_EXT_TRACING_CORE_PRIORITY_BOOST_CONFIG_H_

#include "protos/perfetto/config/priority_boost/priority_boost_config.gen.h"

#include "perfetto/ext/base/scoped_sched_boost.h"
#include "perfetto/ext/base/status_or.h"

namespace perfetto {
inline base::StatusOr<base::SchedPolicyAndPrio> CreateSchedPolicyFromConfig(
    const protos::gen::PriorityBoostConfig& config) {
  auto policy = config.policy();
  uint32_t priority = config.priority();
  switch (policy) {
    case protos::gen::PriorityBoostConfig::POLICY_SCHED_OTHER:
      if (priority <= 20) {
        return base::SchedPolicyAndPrio{
            base::SchedPolicyAndPrio::Policy::kSchedOther, priority};
      }
      return base::ErrStatus(
          "For the 'POLICY_SCHED_OTHER' priority must be in the range [0; 20]");
    case protos::gen::PriorityBoostConfig::POLICY_SCHED_FIFO:
      if (priority >= 1 && priority <= 99) {
        return base::SchedPolicyAndPrio{
            base::SchedPolicyAndPrio::Policy::kSchedFifo, priority};
      }
      return base::ErrStatus(
          "For the 'POLICY_SCHED_FIFO' priority must be in the range [1; 99]");
    case protos::gen::PriorityBoostConfig::POLICY_UNSPECIFIED:
      return base::ErrStatus("Policy must not be 'POLICY_UNSPECIFIED'");
  }
  PERFETTO_CHECK(false);  // For GCC.
}
}  // namespace perfetto
#endif  // INCLUDE_PERFETTO_EXT_TRACING_CORE_PRIORITY_BOOST_CONFIG_H_
