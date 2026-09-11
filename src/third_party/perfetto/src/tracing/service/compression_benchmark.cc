// Copyright (C) 2026 The Android Open Source Project
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//      http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include <benchmark/benchmark.h>

#include <cstdint>
#include <cstring>
#include <random>
#include <string>
#include <vector>

#include "perfetto/base/build_config.h"
#include "perfetto/ext/tracing/core/slice.h"
#include "perfetto/ext/tracing/core/trace_packet.h"
#include "perfetto/protozero/packed_repeated_fields.h"
#include "perfetto/protozero/scattered_heap_buffer.h"

#include "protos/perfetto/trace/ftrace/ftrace_event.pbzero.h"
#include "protos/perfetto/trace/ftrace/ftrace_event_bundle.pbzero.h"
#include "protos/perfetto/trace/ftrace/sched.pbzero.h"
#include "protos/perfetto/trace/test_event.pbzero.h"
#include "protos/perfetto/trace/trace_packet.pbzero.h"
#include "protos/perfetto/trace/track_event/track_event.pbzero.h"
#include "protos/third_party/android/art/heap_graph.pbzero.h"

#if PERFETTO_BUILDFLAG(PERFETTO_ZLIB)
#include "src/tracing/service/zlib_compressor.h"
#endif
#if PERFETTO_BUILDFLAG(PERFETTO_ZSTD)
#include "src/tracing/service/zstd_compressor.h"
#endif

namespace perfetto {
namespace {

// Workload policies: Make(i) returns one serialized TracePacket, varied by `i`
// so a batch isn't trivially deduped away (zstd dedupes across its window) and
// the measured ratios stay representative.

// Structured, highly repetitive: a sched_switch ftrace bundle (~64 events), the
// bulk of a system trace.
struct FtraceSched {
  static std::vector<uint8_t> Make(uint32_t i) {
    protozero::HeapBuffered<protos::pbzero::TracePacket> packet;
    auto* bundle = packet->set_ftrace_events();
    bundle->set_cpu(i % 8);
    uint64_t ts = static_cast<uint64_t>(i) * 1000000;
    for (uint32_t e = 0; e < 64; e++) {
      auto* evt = bundle->add_event();
      evt->set_pid(1000 + ((i + e) % 256));
      evt->set_timestamp(ts + e * 1000);
      auto* ss = evt->set_sched_switch();
      ss->set_prev_comm("thread-" + std::to_string((i + e) % 50));
      ss->set_prev_pid(1000 + ((i + e) % 256));
      ss->set_prev_state(e % 4);
      ss->set_next_comm("thread-" + std::to_string((i + e + 1) % 50));
      ss->set_next_pid(1000 + ((i + e + 1) % 256));
    }
    return packet.SerializeAsArray();
  }
};

// Java heap dump: a HeapGraph with types and cross-referencing objects (mostly
// packed varint ids). heap_graph is an out-of-tree field (number 56), not
// declared on TracePacket, so append it by field number.
struct JavaHeapDump {
  static std::vector<uint8_t> Make(uint32_t i) {
    namespace art = com::android::art::tracing::pbzero;
    protozero::HeapBuffered<art::HeapGraph> hg;
    hg->set_pid(static_cast<int32_t>(1000 + i));
    for (uint32_t t = 0; t < 8; t++) {
      auto* type = hg->add_types();
      type->set_id(t + 1);
      type->set_class_name("com.example.Class" + std::to_string((i + t) % 64));
      type->set_object_size(16 * (t + 1));
    }
    for (uint32_t o = 0; o < 64; o++) {
      uint64_t id = (static_cast<uint64_t>(i) << 16) + o + 1;
      auto* obj = hg->add_objects();
      obj->set_id(id);
      obj->set_type_id((o % 8) + 1);
      obj->set_self_size(16 + (o % 4) * 8);
      protozero::PackedVarInt refs;
      refs.Append(id + 1);
      refs.Append(id + 7);
      obj->set_reference_object_id(refs);
    }
    std::vector<uint8_t> heap_graph = hg.SerializeAsArray();

    protozero::HeapBuffered<protos::pbzero::TracePacket> packet;
    // 56 = HeapGraph heap_graph (out-of-tree field, see heap_graph.proto).
    packet->AppendBytes(56, heap_graph.data(), heap_graph.size());
    return packet.SerializeAsArray();
  }
};

// String-heavy: a TrackEvent slice with a name and categories.
struct TrackEvent {
  static std::vector<uint8_t> Make(uint32_t i) {
    protozero::HeapBuffered<protos::pbzero::TracePacket> packet;
    packet->set_timestamp(static_cast<uint64_t>(i) * 1000);
    auto* te = packet->set_track_event();
    te->set_track_uuid(i % 32);
    te->add_categories("category-" + std::to_string(i % 16));
    te->set_name("SomeEventName::DoWork_" + std::to_string(i % 128));
    return packet.SerializeAsArray();
  }
};

// Worst case: incompressible random bytes (e.g. an already-compressed payload).
struct Incompressible {
  static std::vector<uint8_t> Make(uint32_t i) {
    std::minstd_rand rng(i + 1);
    std::string s(1024, '\0');
    for (char& c : s)
      c = static_cast<char>(rng());
    protozero::HeapBuffered<protos::pbzero::TracePacket> packet;
    packet->set_for_testing()->set_str(s);
    return packet.SerializeAsArray();
  }
};

// Backend policies: each runs one compile-time-selected codec at the given
// level.
#if PERFETTO_BUILDFLAG(PERFETTO_ZLIB)
struct Zlib {
  static void Compress(std::vector<TracePacket>* packets, int /*level*/) {
    ZlibCompressFn(packets);
  }
};
#endif
#if PERFETTO_BUILDFLAG(PERFETTO_ZSTD)
struct Zstd {
  static void Compress(std::vector<TracePacket>* packets, int level) {
    ZstdCompressFn(packets, level);
  }
};
#endif

TracePacket MakePacket(const std::vector<uint8_t>& buf) {
  Slice slice = Slice::Allocate(buf.size());
  memcpy(slice.own_data(), buf.data(), buf.size());
  TracePacket packet;
  packet.AddSlice(std::move(slice));
  return packet;
}

// The compressor consumes (clears) its input, so each iteration needs a fresh
// copy of the packets.
std::vector<TracePacket> CopyPackets(const std::vector<TracePacket>& src) {
  std::vector<TracePacket> out;
  out.reserve(src.size());
  for (const TracePacket& packet : src) {
    TracePacket copy;
    for (const Slice& slice : packet.slices()) {
      Slice new_slice = Slice::Allocate(slice.size);
      memcpy(new_slice.own_data(), slice.start, slice.size);
      copy.AddSlice(std::move(new_slice));
    }
    out.push_back(std::move(copy));
  }
  return out;
}

bool IsBenchmarkFunctionalOnly() {
  return getenv("BENCHMARK_FUNCTIONAL_TEST_ONLY") != nullptr;
}

// Sweep the block size (packets per compress call) at the codec's default
// level.
static void BlockSizeArgs(benchmark::internal::Benchmark* b) {
  b->Unit(benchmark::kMicrosecond);
  b->ArgNames({"packets", "level"});
  if (IsBenchmarkFunctionalOnly()) {
    b->Args({1, 0})->Iterations(1);
    return;
  }
  b->RangeMultiplier(8)->Ranges({{1, 4096}, {0, 0}});
}

// Sweep the zstd level (fastest..max) at a fixed, representative block size.
static void ZstdLevelArgs(benchmark::internal::Benchmark* b) {
  b->Unit(benchmark::kMicrosecond);
  b->ArgNames({"packets", "level"});
  if (IsBenchmarkFunctionalOnly()) {
    b->Args({1, 0})->Iterations(1);
    return;
  }
  for (int level : {1, 3, 9, 19, 22})
    b->Args({512, level});
}

// Compresses a batch of `state.range(0)` packets per iteration. The reported
// time is the per-batch compression latency; SetBytesProcessed gives throughput
// and the counters report the compression ratio.
template <typename Backend, typename Workload>
static void BM_Compress(benchmark::State& state) {
  const size_t num_packets = static_cast<size_t>(state.range(0));
  const int level = static_cast<int>(state.range(1));
  std::vector<TracePacket> packets;
  size_t total_in = 0;
  for (size_t i = 0; i < num_packets; i++) {
    packets.push_back(MakePacket(Workload::Make(static_cast<uint32_t>(i))));
    total_in += packets.back().size();
  }

  size_t total_out = 0;
  for (auto _ : state) {
    state.PauseTiming();
    std::vector<TracePacket> input = CopyPackets(packets);
    state.ResumeTiming();

    Backend::Compress(&input, level);

    total_out = 0;
    for (const TracePacket& packet : input)
      total_out += packet.size();
    benchmark::DoNotOptimize(total_out);
  }

  state.counters["packets"] = static_cast<double>(num_packets);
  state.counters["in_bytes"] = static_cast<double>(total_in);
  state.counters["out_bytes"] = static_cast<double>(total_out);
  state.counters["ratio"] =
      total_out ? static_cast<double>(total_in) / static_cast<double>(total_out)
                : 0;
  state.SetBytesProcessed(static_cast<int64_t>(state.iterations()) *
                          static_cast<int64_t>(total_in));
}

#if PERFETTO_BUILDFLAG(PERFETTO_ZLIB)
BENCHMARK_TEMPLATE(BM_Compress, Zlib, FtraceSched)->Apply(BlockSizeArgs);
BENCHMARK_TEMPLATE(BM_Compress, Zlib, JavaHeapDump)->Apply(BlockSizeArgs);
BENCHMARK_TEMPLATE(BM_Compress, Zlib, TrackEvent)->Apply(BlockSizeArgs);
BENCHMARK_TEMPLATE(BM_Compress, Zlib, Incompressible)->Apply(BlockSizeArgs);
#endif

#if PERFETTO_BUILDFLAG(PERFETTO_ZSTD)
BENCHMARK_TEMPLATE(BM_Compress, Zstd, FtraceSched)->Apply(BlockSizeArgs);
BENCHMARK_TEMPLATE(BM_Compress, Zstd, JavaHeapDump)->Apply(BlockSizeArgs);
BENCHMARK_TEMPLATE(BM_Compress, Zstd, TrackEvent)->Apply(BlockSizeArgs);
BENCHMARK_TEMPLATE(BM_Compress, Zstd, Incompressible)->Apply(BlockSizeArgs);

// Level sweep (zstd only): how ratio and latency trade off across levels.
BENCHMARK_TEMPLATE(BM_Compress, Zstd, FtraceSched)->Apply(ZstdLevelArgs);
BENCHMARK_TEMPLATE(BM_Compress, Zstd, JavaHeapDump)->Apply(ZstdLevelArgs);
BENCHMARK_TEMPLATE(BM_Compress, Zstd, TrackEvent)->Apply(ZstdLevelArgs);
BENCHMARK_TEMPLATE(BM_Compress, Zstd, Incompressible)->Apply(ZstdLevelArgs);
#endif

}  // namespace
}  // namespace perfetto
