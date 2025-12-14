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

#include "perfetto/public/abi/track_event_ll_abi.h"

#include "perfetto/tracing/internal/track_event_internal.h"
#include "src/shared_lib/track_event/ds.h"
#include "src/shared_lib/track_event/serialization.h"

namespace {

void FillIterator(
    const perfetto::internal::DataSourceType::InstancesIterator* ii,
    struct PerfettoTeTimestamp ts,
    struct PerfettoTeLlImplIterator* iterator) {
  perfetto::internal::DataSourceType* ds =
      perfetto::shlib::TrackEvent::GetType();

  auto& track_event_tls = *static_cast<perfetto::shlib::TrackEventTlsState*>(
      ii->instance->data_source_custom_tls.get());

  auto* incr_state = static_cast<perfetto::shlib::TrackEventIncrementalState*>(
      ds->GetIncrementalState(ii->instance, ii->i));
  perfetto::TraceTimestamp tts;
  tts.clock_id = ts.clock_id;
  tts.value = ts.value;
  perfetto::shlib::ResetIncrementalStateIfRequired(
      ii->instance->trace_writer.get(), incr_state, track_event_tls, tts);

  iterator->incr = reinterpret_cast<struct PerfettoTeLlImplIncr*>(incr_state);
  iterator->tls =
      reinterpret_cast<struct PerfettoTeLlImplTls*>(&track_event_tls);
}

}  // namespace

struct PerfettoTeLlImplIterator PerfettoTeLlImplBegin(
    struct PerfettoTeCategoryImpl* cat,
    struct PerfettoTeTimestamp ts) {
  struct PerfettoTeLlImplIterator ret = {};
  uint32_t cached_instances =
      perfetto::shlib::TracePointTraits::GetActiveInstances({cat})->load(
          std::memory_order_relaxed);
  if (!cached_instances) {
    return ret;
  }

  perfetto::internal::DataSourceType* ds =
      perfetto::shlib::TrackEvent::GetType();

  perfetto::internal::DataSourceThreadLocalState*& tls_state =
      *perfetto::shlib::TrackEvent::GetTlsState();

  if (!ds->TracePrologue<perfetto::shlib::TrackEventDataSourceTraits,
                         perfetto::shlib::TracePointTraits>(
          &tls_state, &cached_instances, {cat})) {
    return ret;
  }

  perfetto::internal::DataSourceType::InstancesIterator ii =
      ds->BeginIteration<perfetto::shlib::TracePointTraits>(cached_instances,
                                                            tls_state, {cat});

  ret.ds.inst_id = ii.i;
  tls_state->root_tls->cached_instances = ii.cached_instances;
  ret.ds.tracer = reinterpret_cast<struct PerfettoDsTracerImpl*>(ii.instance);
  if (!ret.ds.tracer) {
    ds->TraceEpilogue(tls_state);
    return ret;
  }

  FillIterator(&ii, ts, &ret);

  ret.ds.tls = reinterpret_cast<struct PerfettoDsTlsImpl*>(tls_state);
  return ret;
}

void PerfettoTeLlImplNext(struct PerfettoTeCategoryImpl* cat,
                          struct PerfettoTeTimestamp ts,
                          struct PerfettoTeLlImplIterator* iterator) {
  auto* tls = reinterpret_cast<perfetto::internal::DataSourceThreadLocalState*>(
      iterator->ds.tls);

  perfetto::internal::DataSourceType::InstancesIterator ii;
  ii.i = iterator->ds.inst_id;
  ii.cached_instances = tls->root_tls->cached_instances;
  ii.instance =
      reinterpret_cast<perfetto::internal::DataSourceInstanceThreadLocalState*>(
          iterator->ds.tracer);

  perfetto::internal::DataSourceType* ds =
      perfetto::shlib::TrackEvent::GetType();

  ds->NextIteration</*Traits=*/perfetto::shlib::TracePointTraits>(&ii, tls,
                                                                  {cat});

  iterator->ds.inst_id = ii.i;
  tls->root_tls->cached_instances = ii.cached_instances;
  iterator->ds.tracer =
      reinterpret_cast<struct PerfettoDsTracerImpl*>(ii.instance);

  if (!iterator->ds.tracer) {
    ds->TraceEpilogue(tls);
    return;
  }

  FillIterator(&ii, ts, iterator);
}

void PerfettoTeLlImplBreak(struct PerfettoTeCategoryImpl*,
                           struct PerfettoTeLlImplIterator* iterator) {
  auto* tls = reinterpret_cast<perfetto::internal::DataSourceThreadLocalState*>(
      iterator->ds.tls);

  perfetto::internal::DataSourceType* ds =
      perfetto::shlib::TrackEvent::GetType();

  ds->TraceEpilogue(tls);
}

bool PerfettoTeLlImplDynCatEnabled(
    struct PerfettoDsTracerImpl* tracer,
    PerfettoDsInstanceIndex inst_id,
    const struct PerfettoTeCategoryDescriptor* dyn_cat) {
  perfetto::internal::DataSourceType* ds =
      perfetto::shlib::TrackEvent::GetType();

  auto* tls_inst =
      reinterpret_cast<perfetto::internal::DataSourceInstanceThreadLocalState*>(
          tracer);

  auto* incr_state = static_cast<perfetto::shlib::TrackEventIncrementalState*>(
      ds->GetIncrementalState(tls_inst, inst_id));

  return perfetto::shlib::TrackEvent::IsDynamicCategoryEnabled(
      inst_id, incr_state, *dyn_cat);
}

bool PerfettoTeLlImplTrackSeen(struct PerfettoTeLlImplIncr* incr,
                               uint64_t uuid) {
  auto* incr_state =
      reinterpret_cast<perfetto::shlib::TrackEventIncrementalState*>(incr);

  return !incr_state->seen_track_uuids.insert(uuid).second;
}

uint64_t PerfettoTeLlImplIntern(struct PerfettoTeLlImplIncr* incr,
                                int32_t type,
                                const void* data,
                                size_t data_size,
                                bool* seen) {
  auto* incr_state =
      reinterpret_cast<perfetto::shlib::TrackEventIncrementalState*>(incr);

  auto res = incr_state->iids.FindOrAssign(type, data, data_size);
  *seen = !res.newly_assigned;
  return res.iid;
}
