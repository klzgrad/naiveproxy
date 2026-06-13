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

#include "src/shared_lib/track_event/global_state.h"

#include "perfetto/protozero/scattered_heap_buffer.h"
#include "perfetto/tracing/internal/basic_types.h"
#include "protos/perfetto/common/data_source_descriptor.gen.h"
#include "protos/perfetto/common/track_event_descriptor.pbzero.h"
#include "src/shared_lib/track_event/category_utils.h"

namespace perfetto::shlib {

namespace {

bool IsRegisteredCategoryEnabled(
    const PerfettoTeCategoryImpl& cat,
    const perfetto::protos::gen::TrackEventConfig& config) {
  if (!cat.desc) {
    return false;
  }
  return IsSingleCategoryEnabled(*cat.desc, config);
}

}  // namespace

GlobalState::GlobalState() : interned_categories_(0) {
  perfetto_te_any_categories = new PerfettoTeCategoryImpl;
  perfetto_te_any_categories_enabled = &perfetto_te_any_categories->flag;
}

void GlobalState::OnStart(const perfetto::protos::gen::TrackEventConfig& config,
                          uint32_t instance_id) PERFETTO_LOCKS_EXCLUDED(mu_) {
  std::lock_guard<std::mutex> lock(mu_);
  perfetto_te_any_categories->EnableInstance(instance_id);
  for (PerfettoTeCategoryImpl* cat : categories_) {
    if (IsRegisteredCategoryEnabled(*cat, config)) {
      cat->EnableInstance(instance_id);
    }
  }
  active_configs_[instance_id] = config;
}

void GlobalState::OnStop(uint32_t instance_id) PERFETTO_LOCKS_EXCLUDED(mu_) {
  std::lock_guard<std::mutex> lock(mu_);
  for (PerfettoTeCategoryImpl* cat : categories_) {
    cat->DisableInstance(instance_id);
  }
  perfetto_te_any_categories->DisableInstance(instance_id);
  active_configs_.erase(instance_id);
}

void GlobalState::RegisterCategory(PerfettoTeCategoryImpl* cat)
    PERFETTO_LOCKS_EXCLUDED(mu_) {
  {
    std::lock_guard<std::mutex> lock(mu_);
    for (const auto& [instance_id, config] : active_configs_) {
      if (IsRegisteredCategoryEnabled(*cat, config)) {
        cat->EnableInstance(instance_id);
      }
    }
    categories_.push_back(cat);
    cat->cat_iid = ++interned_categories_;
  }
}

void GlobalState::UnregisterCategory(PerfettoTeCategoryImpl* cat)
    PERFETTO_LOCKS_EXCLUDED(mu_) {
  std::lock_guard<std::mutex> lock(mu_);
  categories_.erase(std::remove(categories_.begin(), categories_.end(), cat),
                    categories_.end());
}

void GlobalState::CategorySetCallback(struct PerfettoTeCategoryImpl* cat,
                                      PerfettoTeCategoryImplCallback cb,
                                      void* user_arg)
    PERFETTO_LOCKS_EXCLUDED(mu_) {
  std::lock_guard<std::mutex> lock(mu_);
  cat->cb = cb;
  cat->cb_user_arg = user_arg;
  if (!cat->cb) {
    return;
  }

  bool first = true;
  uint8_t active_instances = cat->instances.load(std::memory_order_relaxed);
  for (PerfettoDsInstanceIndex i = 0; i < internal::kMaxDataSourceInstances;
       i++) {
    if ((active_instances & (1 << i)) == 0) {
      continue;
    }
    cb(cat, i, true, first, user_arg);
    first = false;
  }
}

DataSourceDescriptor GlobalState::GenerateDescriptorFromCategories() const {
  DataSourceDescriptor dsd;

  protozero::HeapBuffered<perfetto::protos::pbzero::TrackEventDescriptor> ted;
  {
    std::lock_guard<std::mutex> lock(mu_);
    for (PerfettoTeCategoryImpl* cat : categories_) {
      SerializeCategory(*cat->desc, ted.get());
    }
  }
  dsd.set_track_event_descriptor_raw(ted.SerializeAsString());
  return dsd;
}

}  // namespace perfetto::shlib
