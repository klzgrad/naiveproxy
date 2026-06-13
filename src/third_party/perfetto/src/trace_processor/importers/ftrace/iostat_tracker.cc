/*
 * Copyright (C) 2022 The Android Open Source Project
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

#include "src/trace_processor/importers/ftrace/iostat_tracker.h"

#include <cstdint>
#include <string>

#include "perfetto/ext/base/string_utils.h"
#include "perfetto/ext/base/string_view.h"
#include "perfetto/protozero/field.h"
#include "protos/perfetto/trace/ftrace/f2fs.pbzero.h"
#include "src/trace_processor/importers/common/event_tracker.h"
#include "src/trace_processor/importers/common/track_tracker.h"
#include "src/trace_processor/importers/common/tracks.h"
#include "src/trace_processor/importers/common/tracks_common.h"
#include "src/trace_processor/storage/trace_storage.h"

namespace perfetto::trace_processor {

namespace {

std::string GetRawDeviceName(uint64_t dev_num) {
  return std::to_string((dev_num & 0xFF00) >> 8) + ":" +
         std::to_string(dev_num & 0xFF);
}

}  // namespace

IostatTracker::IostatTracker(TraceProcessorContext* context)
    : context_(context) {}

void IostatTracker::ParseF2fsIostat(int64_t timestamp,
                                    protozero::ConstBytes blob) {
  protos::pbzero::F2fsIostatFtraceEvent::Decoder evt(blob);

  static constexpr auto kBlueprint = tracks::CounterBlueprint(
      "f2fs_iostat", tracks::UnknownUnitBlueprint(),
      tracks::DimensionBlueprints(
          tracks::kLinuxDeviceDimensionBlueprint,
          tracks::StringDimensionBlueprint("counter_key")),
      tracks::FnNameBlueprint(
          [](base::StringView device, base::StringView name) {
            return base::StackString<1024>("f2fs_iostat.[%.*s].%.*s",
                                           int(device.size()), device.data(),
                                           int(name.size()), name.data());
          }));

  auto push_counter = [&, this](const char* counter_name, uint64_t value) {
    TrackId track = context_->track_tracker->InternTrack(
        kBlueprint,
        tracks::DimensionBlueprints(
            base::StringView(GetRawDeviceName(evt.dev())), counter_name));
    context_->event_tracker->PushCounter(timestamp, static_cast<double>(value),
                                         track);
  };
  push_counter("write_app_total", evt.app_wio());
  push_counter("write_app_direct", evt.app_dio());
  push_counter("write_app_buffered", evt.app_bio());
  push_counter("write_app_mapped", evt.app_mio());
  push_counter("write_fs_data", evt.fs_dio());
  push_counter("write_fs_node", evt.fs_nio());
  push_counter("write_fs_meta", evt.fs_mio());
  push_counter("write_gc_data", evt.fs_gc_dio());
  push_counter("write_gc_node", evt.fs_gc_nio());
  push_counter("write_cp_data", evt.fs_cp_dio());
  push_counter("write_cp_node", evt.fs_cp_nio());
  push_counter("write_cp_meta", evt.fs_cp_mio());
  push_counter("read_app_total", evt.app_rio());
  push_counter("read_app_direct", evt.app_drio());
  push_counter("read_app_buffered", evt.app_brio());
  push_counter("read_app_mapped", evt.app_mrio());
  push_counter("read_fs_data", evt.fs_drio());
  push_counter("read_fs_gdata", evt.fs_gdrio());
  push_counter("read_fs_cdata", evt.fs_cdrio());
  push_counter("read_fs_node", evt.fs_nrio());
  push_counter("read_fs_meta", evt.fs_mrio());
  push_counter("other_fs_discard", evt.fs_discard());
}

void IostatTracker::ParseF2fsIostatLatency(int64_t timestamp,
                                           protozero::ConstBytes blob) {
  protos::pbzero::F2fsIostatLatencyFtraceEvent::Decoder evt(blob);

  static constexpr auto kBlueprint = tracks::CounterBlueprint(
      "f2fs_iostat_latency", tracks::UnknownUnitBlueprint(),
      tracks::DimensionBlueprints(
          tracks::kLinuxDeviceDimensionBlueprint,
          tracks::StringDimensionBlueprint("counter_key")),
      tracks::FnNameBlueprint(
          [](base::StringView device, base::StringView name) {
            return base::StackString<1024>("f2fs_iostat_latency.[%.*s].%.*s",
                                           int(device.size()), device.data(),
                                           int(name.size()), name.data());
          }));

  auto push_counter = [&, this](const char* counter_name, uint64_t value) {
    TrackId track = context_->track_tracker->InternTrack(
        kBlueprint,
        tracks::DimensionBlueprints(
            base::StringView(GetRawDeviceName(evt.dev())), counter_name));
    context_->event_tracker->PushCounter(timestamp, static_cast<double>(value),
                                         track);
  };
  push_counter("read_data_peak", evt.d_rd_peak());
  push_counter("read_data_avg", evt.d_rd_avg());
  push_counter("read_data_cnt", evt.d_rd_cnt());
  push_counter("read_node_peak", evt.n_rd_peak());
  push_counter("read_node_avg", evt.n_rd_avg());
  push_counter("read_node_cnt", evt.n_rd_cnt());
  push_counter("read_meta_peak", evt.m_rd_peak());
  push_counter("read_meta_avg", evt.m_rd_avg());
  push_counter("read_meta_cnt", evt.m_rd_cnt());
  push_counter("write_sync_data_peak", evt.d_wr_s_peak());
  push_counter("write_sync_data_avg", evt.d_wr_s_avg());
  push_counter("write_sync_data_cnt", evt.d_wr_s_cnt());
  push_counter("write_sync_node_peak", evt.n_wr_s_peak());
  push_counter("write_sync_node_avg", evt.n_wr_s_avg());
  push_counter("write_sync_node_cnt", evt.n_wr_s_cnt());
  push_counter("write_sync_meta_peak", evt.m_wr_s_peak());
  push_counter("write_sync_meta_avg", evt.m_wr_s_avg());
  push_counter("write_sync_meta_cnt", evt.m_wr_s_cnt());
  push_counter("write_async_data_peak", evt.d_wr_as_peak());
  push_counter("write_async_data_avg", evt.d_wr_as_avg());
  push_counter("write_async_data_cnt", evt.d_wr_as_cnt());
  push_counter("write_async_node_peak", evt.n_wr_as_peak());
  push_counter("write_async_node_avg", evt.n_wr_as_avg());
  push_counter("write_async_node_cnt", evt.n_wr_as_cnt());
  push_counter("write_async_meta_peak", evt.m_wr_as_peak());
  push_counter("write_async_meta_avg", evt.m_wr_as_avg());
  push_counter("write_async_meta_cnt", evt.m_wr_as_cnt());
}

}  // namespace perfetto::trace_processor
