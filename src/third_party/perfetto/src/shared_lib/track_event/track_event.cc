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

#include "perfetto/public/abi/track_event_abi.h"

#include "perfetto/tracing/data_source.h"
#include "perfetto/tracing/internal/track_event_internal.h"
#include "perfetto/tracing/track.h"
#include "src/shared_lib/reset_for_testing.h"
#include "src/shared_lib/track_event/category_impl.h"
#include "src/shared_lib/track_event/ds.h"

struct PerfettoTeCategoryImpl* perfetto_te_any_categories;

PERFETTO_ATOMIC(bool) * perfetto_te_any_categories_enabled;

uint64_t perfetto_te_process_track_uuid;

namespace perfetto::shlib {

void ResetTrackEventTls() {
  *TrackEvent::GetTlsState() = nullptr;
}

}  // namespace perfetto::shlib

void PerfettoTeInit(void) {
  perfetto::DataSourceDescriptor dsd = perfetto::shlib::GlobalState::Instance()
                                           .GenerateDescriptorFromCategories();
  perfetto::shlib::TrackEvent::Init(dsd);
  perfetto_te_process_track_uuid =
      perfetto::internal::TrackRegistry::ComputeProcessUuid();
}

struct PerfettoTeTimestamp PerfettoTeGetTimestamp(void) {
  struct PerfettoTeTimestamp ret;
  ret.clock_id = PERFETTO_TE_TIMESTAMP_TYPE_BOOT;
  ret.value = perfetto::internal::TrackEventInternal::GetTimeNs();
  return ret;
}

struct PerfettoTeCategoryImpl* PerfettoTeCategoryImplCreate(
    struct PerfettoTeCategoryDescriptor* desc) {
  auto* cat = new PerfettoTeCategoryImpl;
  cat->desc = desc;

  perfetto::shlib::GlobalState::Instance().RegisterCategory(cat);
  return cat;
}

void PerfettoTePublishCategories() {
  perfetto::DataSourceDescriptor dsd = perfetto::shlib::GlobalState::Instance()
                                           .GenerateDescriptorFromCategories();
  perfetto::shlib::TrackEvent::UpdateDescriptorFromCategories(dsd);
}

void PerfettoTeCategoryImplSetCallback(struct PerfettoTeCategoryImpl* cat,
                                       PerfettoTeCategoryImplCallback cb,
                                       void* user_arg) {
  perfetto::shlib::TrackEvent::CategorySetCallback(cat, cb, user_arg);
}

PERFETTO_ATOMIC(bool) *
    PerfettoTeCategoryImplGetEnabled(struct PerfettoTeCategoryImpl* cat) {
  return &cat->flag;
}

uint64_t PerfettoTeCategoryImplGetIid(struct PerfettoTeCategoryImpl* cat) {
  return cat->cat_iid;
}

void PerfettoTeCategoryImplDestroy(struct PerfettoTeCategoryImpl* cat) {
  perfetto::shlib::GlobalState::Instance().UnregisterCategory(cat);
  delete cat;
}
