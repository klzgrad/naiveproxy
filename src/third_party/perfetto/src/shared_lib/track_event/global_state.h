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

#ifndef SRC_SHARED_LIB_TRACK_EVENT_GLOBAL_STATE_H_
#define SRC_SHARED_LIB_TRACK_EVENT_GLOBAL_STATE_H_

#include <cstdint>
#include <mutex>
#include <unordered_map>

#include "perfetto/base/thread_annotations.h"
#include "perfetto/tracing/core/forward_decls.h"
#include "protos/perfetto/config/track_event/track_event_config.gen.h"
#include "src/shared_lib/track_event/category_impl.h"

namespace perfetto::shlib {

class GlobalState {
 public:
  static GlobalState& Instance() {
    static GlobalState* instance = new GlobalState();
    return *instance;
  }

  void OnStart(const perfetto::protos::gen::TrackEventConfig& config,
               uint32_t instance_id) PERFETTO_LOCKS_EXCLUDED(mu_);
  void OnStop(uint32_t instance_id) PERFETTO_LOCKS_EXCLUDED(mu_);
  void RegisterCategory(PerfettoTeCategoryImpl* cat)
      PERFETTO_LOCKS_EXCLUDED(mu_);
  void UnregisterCategory(PerfettoTeCategoryImpl* cat)
      PERFETTO_LOCKS_EXCLUDED(mu_);
  void CategorySetCallback(struct PerfettoTeCategoryImpl* cat,
                           PerfettoTeCategoryImplCallback cb,
                           void* user_arg) PERFETTO_LOCKS_EXCLUDED(mu_);
  DataSourceDescriptor GenerateDescriptorFromCategories() const
      PERFETTO_LOCKS_EXCLUDED(mu_);

 private:
  GlobalState();

  mutable std::mutex mu_;
  std::vector<PerfettoTeCategoryImpl*> categories_ PERFETTO_GUARDED_BY(mu_);
  std::unordered_map<uint32_t, perfetto::protos::gen::TrackEventConfig>
      active_configs_ PERFETTO_GUARDED_BY(mu_);
  uint64_t interned_categories_ PERFETTO_GUARDED_BY(mu_);
};

}  // namespace perfetto::shlib

#endif  // SRC_SHARED_LIB_TRACK_EVENT_GLOBAL_STATE_H_
