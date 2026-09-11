/*
 * Copyright (C) 2026 The Android Open Source Project
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

// Microbenchmarks for TypedProtoDecoder on real traces from test/data,
// replaying the access patterns of trace_processor's hot proto import paths
// (TracePacket scan, ftrace bundle/event decode, TrackEvent decode) using the
// real pbzero-generated decoders.

#include <benchmark/benchmark.h>

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

#include "perfetto/base/logging.h"
#include "perfetto/ext/base/file_utils.h"
#include "perfetto/protozero/proto_decoder.h"
#include "src/base/test/utils.h"

#include "protos/perfetto/trace/ftrace/ftrace_event.pbzero.h"
#include "protos/perfetto/trace/ftrace/ftrace_event_bundle.pbzero.h"
#include "protos/perfetto/trace/ftrace/sched.pbzero.h"
#include "protos/perfetto/trace/trace_packet.pbzero.h"
#include "protos/perfetto/trace/track_event/track_event.pbzero.h"

namespace {

using protozero::ConstBytes;
using protozero::ProtoDecoder;

namespace pbzero = perfetto::protos::pbzero;

// Packets / ftrace bundles / track events are pre-tokenized outside the timed
// region (with the schema-less ProtoDecoder) so the benchmarks measure the
// typed decoder cost only.
struct LoadedTrace {
  std::string blob;
  std::vector<ConstBytes> packets;
  std::vector<ConstBytes> ftrace_bundles;
  std::vector<ConstBytes> track_events;
  size_t num_ftrace_events = 0;
  int64_t packets_bytes = 0;
  int64_t ftrace_bundles_bytes = 0;
  int64_t track_events_bytes = 0;
};

const LoadedTrace& GetTrace(const char* name) {
  static auto* cache = new std::unordered_map<std::string, LoadedTrace>();
  auto it = cache->find(name);
  if (it != cache->end())
    return it->second;

  LoadedTrace& trace = (*cache)[name];
  std::string path =
      perfetto::base::GetTestDataPath(std::string("test/data/") + name);
  PERFETTO_CHECK(perfetto::base::ReadFile(path, &trace.blob));

  ProtoDecoder outer(trace.blob.data(), trace.blob.size());
  for (auto f = outer.ReadField(); f.valid(); f = outer.ReadField()) {
    if (f.id() != 1)
      continue;
    trace.packets.push_back(f.as_bytes());
    trace.packets_bytes += static_cast<int64_t>(f.size());

    ProtoDecoder packet(f.as_bytes());
    for (auto pf = packet.ReadField(); pf.valid(); pf = packet.ReadField()) {
      if (pf.id() == pbzero::TracePacket::kFtraceEventsFieldNumber) {
        trace.ftrace_bundles.push_back(pf.as_bytes());
        trace.ftrace_bundles_bytes += static_cast<int64_t>(pf.size());
        ProtoDecoder bundle(pf.as_bytes());
        for (auto bf = bundle.ReadField(); bf.valid();
             bf = bundle.ReadField()) {
          if (bf.id() == pbzero::FtraceEventBundle::kEventFieldNumber)
            trace.num_ftrace_events++;
        }
      } else if (pf.id() == pbzero::TracePacket::kTrackEventFieldNumber) {
        trace.track_events.push_back(pf.as_bytes());
        trace.track_events_bytes += static_cast<int64_t>(pf.size());
      }
    }
  }
  PERFETTO_CHECK(!trace.packets.empty());
  return trace;
}

// Mirrors ProtoTraceReader::ParsePacket(): decode every TracePacket and inspect
// a handful of header fields + "which data field is set".
uint64_t ScanPackets(const LoadedTrace& trace) {
  uint64_t acc = 0;
  for (const ConstBytes& p : trace.packets) {
    pbzero::TracePacket::Decoder d(p.data, p.size);
    if (d.has_timestamp())
      acc += d.timestamp();
    acc += d.trusted_packet_sequence_id();
    acc += d.sequence_flags();
    acc += d.has_ftrace_events();
    acc += d.has_track_event();
    acc += d.has_interned_data();
  }
  return acc;
}

// Mirrors the ftrace tokenization path: decode each bundle, walk |event|,
// decode every FtraceEvent and the sched events within.
uint64_t ScanFtraceEvents(const LoadedTrace& trace) {
  uint64_t acc = 0;
  for (const ConstBytes& b : trace.ftrace_bundles) {
    pbzero::FtraceEventBundle::Decoder bundle(b.data, b.size);
    acc += bundle.cpu();
    for (auto it = bundle.event(); it; ++it) {
      pbzero::FtraceEvent::Decoder event(*it);
      // Order-sensitive mix so repeated |event| iteration order is exercised.
      acc = acc * 31 + event.timestamp();
      acc += event.pid();
      if (event.has_sched_switch()) {
        pbzero::SchedSwitchFtraceEvent::Decoder ss(event.sched_switch());
        acc += static_cast<uint64_t>(ss.next_pid());
        acc += static_cast<uint64_t>(ss.prev_state());
      } else if (event.has_sched_waking()) {
        pbzero::SchedWakingFtraceEvent::Decoder sw(event.sched_waking());
        acc += static_cast<uint64_t>(sw.pid());
      }
    }
  }
  return acc;
}

// Mirrors TrackEventParser: decode each TrackEvent, read the hot fields.
uint64_t ScanTrackEvents(const LoadedTrace& trace) {
  uint64_t acc = 0;
  for (const ConstBytes& te : trace.track_events) {
    pbzero::TrackEvent::Decoder d(te.data, te.size);
    acc += static_cast<uint64_t>(d.type());
    acc += d.track_uuid();
    acc += d.name_iid();
    for (auto it = d.category_iids(); it; ++it)
      acc += *it;
    acc += d.has_extra_counter_values();
  }
  return acc;
}

void BM_TracePacketScan(benchmark::State& state, const char* trace_name) {
  const LoadedTrace& trace = GetTrace(trace_name);
  for (auto _ : state) {
    benchmark::DoNotOptimize(ScanPackets(trace));
  }
  state.SetBytesProcessed(static_cast<int64_t>(state.iterations()) *
                          trace.packets_bytes);
  state.SetItemsProcessed(static_cast<int64_t>(state.iterations()) *
                          static_cast<int64_t>(trace.packets.size()));
}

// Like ScanPackets(), but with selective decoding configured exactly like
// trace_processor's planned SelectiveTracePacketDecoder: the allowlist is
// the 11 packet *metadata* fields the pipeline reads by name; every other
// field -- the per-packet data fields (track_event, ftrace_events, ...) and
// out-of-tree extensions, which ScanPackets() cannot see at all -- is
// consumed, in wire order, from the spill area, mirroring module dispatch.
uint64_t ScanPacketsSelective(const LoadedTrace& trace) {
  using TP = pbzero::TracePacket;
  static constexpr int kMaxDirectFieldId = TP::kMachineIdFieldNumber;
  static constexpr protozero::SelectiveDecodeMask<
      TP::kTimestampFieldNumber, TP::kTimestampClockIdFieldNumber,
      TP::kTrustedUidFieldNumber, TP::kTrustedPacketSequenceIdFieldNumber,
      TP::kTrustedPidFieldNumber, TP::kInternedDataFieldNumber,
      TP::kSequenceFlagsFieldNumber, TP::kIncrementalStateClearedFieldNumber,
      TP::kPreviousPacketDroppedFieldNumber,
      TP::kFirstPacketOnSequenceFieldNumber, TP::kMachineIdFieldNumber>
      kDenseMask{};
  uint64_t acc = 0;
  for (const ConstBytes& p : trace.packets) {
    protozero::SelectiveTypedProtoDecoder<kMaxDirectFieldId> d(p.data, p.size,
                                                               kDenseMask);
    if (d.at<TP::kTimestampFieldNumber>().valid())
      acc += d.at<TP::kTimestampFieldNumber>().as_uint64();
    acc += d.at<TP::kTrustedPacketSequenceIdFieldNumber>().as_uint32();
    acc += d.at<TP::kSequenceFlagsFieldNumber>().as_uint32();
    acc += d.at<TP::kInternedDataFieldNumber>().valid();
    // The data fields are not allowlisted: like the real dispatchers, find
    // them by switching on whatever the packet contains.
    for (const protozero::Field& f : d.unknown_fields()) {
      acc += f.id() == TP::kFtraceEventsFieldNumber;
      acc += f.id() == TP::kTrackEventFieldNumber;
    }
  }
  return acc;
}

void BM_FtraceEventScan(benchmark::State& state, const char* trace_name) {
  const LoadedTrace& trace = GetTrace(trace_name);
  for (auto _ : state) {
    benchmark::DoNotOptimize(ScanFtraceEvents(trace));
  }
  state.SetBytesProcessed(static_cast<int64_t>(state.iterations()) *
                          trace.ftrace_bundles_bytes);
  state.SetItemsProcessed(static_cast<int64_t>(state.iterations()) *
                          static_cast<int64_t>(trace.num_ftrace_events));
}

// The pre-selective extension mechanism on top of ScanPackets(): each module
// registered for an out-of-tree extension field probes the packet with a
// FindField() buffer re-scan (what GetExtensionSlowly() used to do). Four
// probed ids model a modest set of registered extension modules.
uint64_t ScanPacketsExtensionsToday(const LoadedTrace& trace) {
  uint64_t acc = 0;
  static constexpr uint32_t kProbedExtensionIds[] = {551, 900, 1000, 1750};
  for (const ConstBytes& p : trace.packets) {
    pbzero::TracePacket::Decoder d(p.data, p.size);
    if (d.has_timestamp())
      acc += d.timestamp();
    acc += d.trusted_packet_sequence_id();
    acc += d.sequence_flags();
    acc += d.has_ftrace_events();
    acc += d.has_track_event();
    acc += d.has_interned_data();
    for (uint32_t ext_id : kProbedExtensionIds)
      acc += ProtoDecoder(p.data, p.size).FindField(ext_id).valid();
  }
  return acc;
}

void BM_TracePacketScanExtensionsToday(benchmark::State& state,
                                       const char* trace_name) {
  const LoadedTrace& trace = GetTrace(trace_name);
  for (auto _ : state) {
    benchmark::DoNotOptimize(ScanPacketsExtensionsToday(trace));
  }
  state.SetBytesProcessed(static_cast<int64_t>(state.iterations()) *
                          trace.packets_bytes);
  state.SetItemsProcessed(static_cast<int64_t>(state.iterations()) *
                          static_cast<int64_t>(trace.packets.size()));
}

void BM_TracePacketScanSelective(benchmark::State& state,
                                 const char* trace_name) {
  const LoadedTrace& trace = GetTrace(trace_name);
  for (auto _ : state) {
    benchmark::DoNotOptimize(ScanPacketsSelective(trace));
  }
  state.SetBytesProcessed(static_cast<int64_t>(state.iterations()) *
                          trace.packets_bytes);
  state.SetItemsProcessed(static_cast<int64_t>(state.iterations()) *
                          static_cast<int64_t>(trace.packets.size()));
}

void BM_TrackEventScan(benchmark::State& state, const char* trace_name) {
  const LoadedTrace& trace = GetTrace(trace_name);
  for (auto _ : state) {
    benchmark::DoNotOptimize(ScanTrackEvents(trace));
  }
  state.SetBytesProcessed(static_cast<int64_t>(state.iterations()) *
                          trace.track_events_bytes);
  state.SetItemsProcessed(static_cast<int64_t>(state.iterations()) *
                          static_cast<int64_t>(trace.track_events.size()));
}

BENCHMARK_CAPTURE(BM_TracePacketScan,
                  android_30s,
                  "example_android_trace_30s.pb");
BENCHMARK_CAPTURE(BM_TracePacketScan,
                  chrome_rendering,
                  "chrome_rendering_desktop.pftrace");
BENCHMARK_CAPTURE(BM_TracePacketScan,
                  android_postboot,
                  "android_postboot_unlock.pftrace");

BENCHMARK_CAPTURE(BM_TracePacketScanExtensionsToday,
                  chrome_rendering,
                  "chrome_rendering_desktop.pftrace");

BENCHMARK_CAPTURE(BM_TracePacketScanSelective,
                  android_30s,
                  "example_android_trace_30s.pb");
BENCHMARK_CAPTURE(BM_TracePacketScanSelective,
                  chrome_rendering,
                  "chrome_rendering_desktop.pftrace");
BENCHMARK_CAPTURE(BM_TracePacketScanSelective,
                  android_postboot,
                  "android_postboot_unlock.pftrace");

BENCHMARK_CAPTURE(BM_FtraceEventScan,
                  android_30s,
                  "example_android_trace_30s.pb");
BENCHMARK_CAPTURE(BM_FtraceEventScan, sched_and_ps, "android_sched_and_ps.pb");
BENCHMARK_CAPTURE(BM_FtraceEventScan, android_boot, "android_boot.pftrace");

BENCHMARK_CAPTURE(BM_TrackEventScan,
                  chrome_rendering,
                  "chrome_rendering_desktop.pftrace");
BENCHMARK_CAPTURE(BM_TrackEventScan,
                  chrome_scroll,
                  "chrome_touch_gesture_scroll.pftrace");

}  // namespace
