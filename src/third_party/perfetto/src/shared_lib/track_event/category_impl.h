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

#ifndef SRC_SHARED_LIB_TRACK_EVENT_CATEGORY_IMPL_H_
#define SRC_SHARED_LIB_TRACK_EVENT_CATEGORY_IMPL_H_

#include <atomic>

#include "perfetto/public/abi/track_event_abi.h"

struct PerfettoTeCategoryImpl {
  std::atomic<bool> flag{false};
  std::atomic<uint8_t> instances{0};
  PerfettoTeCategoryDescriptor* desc = nullptr;
  uint64_t cat_iid = 0;
  PerfettoTeCategoryImplCallback cb = nullptr;
  void* cb_user_arg = nullptr;

  void EnableInstance(uint32_t instance_index);
  void DisableInstance(uint32_t instance_index);
};

#endif  // SRC_SHARED_LIB_TRACK_EVENT_CATEGORY_IMPL_H_
