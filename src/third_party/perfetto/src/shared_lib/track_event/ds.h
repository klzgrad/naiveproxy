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

#ifndef SRC_SHARED_LIB_TRACK_EVENT_DS_H_
#define SRC_SHARED_LIB_TRACK_EVENT_DS_H_

#include <cstdint>

#include "perfetto/base/flat_set.h"
#include "perfetto/ext/base/flat_hash_map.h"
#include "perfetto/protozero/scattered_heap_buffer.h"
#include "perfetto/public/abi/track_event_abi.h"
#include "perfetto/tracing/data_source.h"
#include "protos/perfetto/common/data_source_descriptor.gen.h"
#include "protos/perfetto/config/track_event/track_event_config.gen.h"
#include "protos/perfetto/trace/interned_data/interned_data.pbzero.h"
#include "src/shared_lib/track_event/category_impl.h"
#include "src/shared_lib/track_event/global_state.h"
#include "src/shared_lib/track_event/intern_map.h"

namespace perfetto::shlib {

struct TrackEventIncrementalState {
  // A heap-allocated message for storing newly seen interned data while we are
  // in the middle of writing a track event. When a track event wants to write
  // new interned data into the trace, it is first serialized into this message
  // and then flushed to the real trace in EventContext when the packet ends.
  // The message is cached here as a part of incremental state so that we can
  // reuse the underlying buffer allocation for subsequently written interned
  // data.
  protozero::HeapBuffered<protos::pbzero::InternedData>
      serialized_interned_data;
  uint64_t last_timestamp_ns = 0;
  bool was_cleared = true;
  base::FlatSet<uint64_t> seen_track_uuids;
  // Map from serialized representation of a dynamic category to its enabled
  // state.
  base::FlatHashMap<std::string, bool> dynamic_categories;
  InternMap iids;
};

struct TrackEventTlsState {
  template <typename TraceContext>
  explicit TrackEventTlsState(const TraceContext& trace_context) {
    auto locked_ds = trace_context.GetDataSourceLocked();
    bool disable_incremental_timestamps = false;
    timestamp_unit_multiplier = 1;
    if (locked_ds.valid()) {
      const auto& config = locked_ds->GetConfig();
      disable_incremental_timestamps = config.disable_incremental_timestamps();
      if (config.has_timestamp_unit_multiplier() &&
          config.timestamp_unit_multiplier() != 0) {
        timestamp_unit_multiplier = config.timestamp_unit_multiplier();
      }
    }
    if (disable_incremental_timestamps) {
      if (timestamp_unit_multiplier == 1) {
        default_clock_id = PERFETTO_I_CLOCK_INCREMENTAL_UNDERNEATH;
      } else {
        default_clock_id = PERFETTO_TE_TIMESTAMP_TYPE_ABSOLUTE;
      }
    } else {
      default_clock_id = PERFETTO_TE_TIMESTAMP_TYPE_INCREMENTAL;
    }
  }
  uint32_t default_clock_id;
  uint64_t timestamp_unit_multiplier;
};

struct TrackEventDataSourceTraits : public perfetto::DefaultDataSourceTraits {
  using IncrementalStateType = TrackEventIncrementalState;
  using TlsStateType = TrackEventTlsState;
};

class TrackEvent
    : public perfetto::DataSource<TrackEvent, TrackEventDataSourceTraits> {
 public:
  ~TrackEvent() override;
  void OnSetup(const DataSourceBase::SetupArgs& args) override;
  void OnStart(const DataSourceBase::StartArgs&) override;
  void OnStop(const DataSourceBase::StopArgs&) override;

  const perfetto::protos::gen::TrackEventConfig& GetConfig() const {
    return config_;
  }

  static void Init(DataSourceDescriptor dsd) {
    dsd.set_name("track_event");
    Register(dsd);
  }

  static void UpdateDescriptorFromCategories(DataSourceDescriptor dsd) {
    dsd.set_name("track_event");
    UpdateDescriptor(dsd);
  }

  static void CategorySetCallback(struct PerfettoTeCategoryImpl* cat,
                                  PerfettoTeCategoryImplCallback cb,
                                  void* user_arg) {
    GlobalState::Instance().CategorySetCallback(cat, cb, user_arg);
  }

  static internal::DataSourceType* GetType();

  static internal::DataSourceThreadLocalState** GetTlsState() {
    return &tls_state_;
  }

  static bool IsDynamicCategoryEnabled(
      uint32_t inst_idx,
      perfetto::shlib::TrackEventIncrementalState* incr_state,
      const struct PerfettoTeCategoryDescriptor& desc);

 private:
  uint32_t inst_id_;
  perfetto::protos::gen::TrackEventConfig config_;
};

struct TracePointTraits {
  struct TracePointData {
    struct PerfettoTeCategoryImpl* enabled;
  };
  static constexpr std::atomic<uint8_t>* GetActiveInstances(
      TracePointData data) {
    return &data.enabled->instances;
  }
};

}  // namespace perfetto::shlib

PERFETTO_DECLARE_DATA_SOURCE_STATIC_MEMBERS(
    perfetto::shlib::TrackEvent,
    perfetto::shlib::TrackEventDataSourceTraits);

inline perfetto::internal::DataSourceType*
perfetto::shlib::TrackEvent::GetType() {
  return &perfetto::shlib::TrackEvent::Helper::type();
}

#endif  // SRC_SHARED_LIB_TRACK_EVENT_DS_H_
