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

#include "src/shared_lib/track_event/ds.h"

#include "src/shared_lib/track_event/category_utils.h"

namespace perfetto::shlib {

TrackEvent::~TrackEvent() = default;

void TrackEvent::OnSetup(const DataSourceBase::SetupArgs& args) {
  const std::string& config_raw = args.config->track_event_config_raw();
  bool ok = config_.ParseFromArray(config_raw.data(), config_raw.size());
  if (!ok) {
    PERFETTO_LOG("Failed to parse config");
  }
  inst_id_ = args.internal_instance_index;
}

void TrackEvent::OnStart(const DataSourceBase::StartArgs&) {
  GlobalState::Instance().OnStart(config_, inst_id_);
}

void TrackEvent::OnStop(const DataSourceBase::StopArgs&) {
  GlobalState::Instance().OnStop(inst_id_);
}

bool TrackEvent::IsDynamicCategoryEnabled(
    uint32_t inst_idx,
    perfetto::shlib::TrackEventIncrementalState* incr_state,
    const struct PerfettoTeCategoryDescriptor& desc) {
  constexpr size_t kMaxCacheSize = 20;
  perfetto::internal::DataSourceType* ds =
      perfetto::shlib::TrackEvent::GetType();
  auto& cache = incr_state->dynamic_categories;
  protozero::HeapBuffered<perfetto::protos::pbzero::TrackEventDescriptor> ted;
  perfetto::shlib::SerializeCategory(desc, ted.get());
  std::string serialized = ted.SerializeAsString();
  auto* cached = cache.Find(serialized);
  if (cached) {
    return *cached;
  }

  auto* internal_state = ds->static_state()->TryGet(inst_idx);
  if (!internal_state)
    return false;
  std::unique_lock<std::recursive_mutex> lock(internal_state->lock);
  auto* sds = static_cast<perfetto::shlib::TrackEvent*>(
      internal_state->data_source.get());

  bool res = IsSingleCategoryEnabled(desc, sds->GetConfig());
  if (cache.size() < kMaxCacheSize) {
    cache[serialized] = res;
  }
  return res;
}

}  // namespace perfetto::shlib

PERFETTO_DEFINE_DATA_SOURCE_STATIC_MEMBERS(
    perfetto::shlib::TrackEvent,
    perfetto::shlib::TrackEventDataSourceTraits);
