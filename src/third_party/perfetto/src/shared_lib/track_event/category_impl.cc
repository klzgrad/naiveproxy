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

#include "src/shared_lib/track_event/category_impl.h"

#include "perfetto/base/logging.h"
#include "perfetto/tracing/internal/basic_types.h"

void PerfettoTeCategoryImpl::EnableInstance(uint32_t instance_index) {
  PERFETTO_DCHECK(instance_index < perfetto::internal::kMaxDataSourceInstances);
  // Matches the acquire_load in DataSource::Trace().
  uint8_t old = instances.fetch_or(static_cast<uint8_t>(1u << instance_index),
                                   std::memory_order_release);
  bool global_state_changed = old == 0;
  if (global_state_changed) {
    flag.store(true, std::memory_order_relaxed);
  }
  if (cb) {
    cb(this, instance_index, /*created=*/true, global_state_changed,
       cb_user_arg);
  }
}

void PerfettoTeCategoryImpl::DisableInstance(uint32_t instance_index) {
  PERFETTO_DCHECK(instance_index < perfetto::internal::kMaxDataSourceInstances);
  // Matches the acquire_load in DataSource::Trace().
  uint8_t old = instances.fetch_and(
      static_cast<uint8_t>(~(1u << instance_index)), std::memory_order_release);
  if (!(old & static_cast<uint8_t>(1u << instance_index))) {
    return;
  }
  bool global_state_changed = false;
  if (!instances.load(std::memory_order_relaxed)) {
    flag.store(false, std::memory_order_relaxed);
    global_state_changed = true;
  }
  if (cb) {
    cb(this, instance_index, /*created=*/false, global_state_changed,
       cb_user_arg);
  }
}
