// Copyright (C) 2022 The Android Open Source Project
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

#include <algorithm>
#include <string>

#include "perfetto/ext/base/file_utils.h"
#include "perfetto/ext/protozero/proto_ring_buffer.h"
#include "src/base/test/utils.h"

static void BM_ProtoRingBufferReadLargeChunks(benchmark::State& state) {
  std::string trace_data;
  static const char kTestTrace[] = "test/data/example_android_trace_30s.pb";
  perfetto::base::ReadFile(perfetto::base::GetTestDataPath(kTestTrace),
                           &trace_data);
  PERFETTO_CHECK(!trace_data.empty());

  size_t total_packet_size = 0;
  protozero::ProtoRingBuffer buffer;
  for (auto _ : state) {
    protozero::ProtoRingBuffer::Message msg = buffer.ReadMessage();
    if (msg.valid()) {
      total_packet_size += msg.len;
    } else {
      state.PauseTiming();
      buffer.Append(trace_data.data(), trace_data.size());
      state.ResumeTiming();
    }
  }
  benchmark::DoNotOptimize(total_packet_size);
}

BENCHMARK(BM_ProtoRingBufferReadLargeChunks);

static void BM_ProtoRingBufferRead(benchmark::State& state) {
  std::string trace_data;
  static const char kTestTrace[] = "test/data/example_android_trace_30s.pb";
  perfetto::base::ReadFile(perfetto::base::GetTestDataPath(kTestTrace),
                           &trace_data);
  PERFETTO_CHECK(!trace_data.empty());

  constexpr size_t kChunkSize = 1024 * 1024 * 3;

  size_t offset = 0;
  size_t total_packet_size = 0;
  protozero::ProtoRingBuffer buffer;
  for (auto _ : state) {
    protozero::ProtoRingBuffer::Message msg = buffer.ReadMessage();
    if (msg.valid()) {
      total_packet_size += msg.len;
    } else {
      state.PauseTiming();
      size_t sz = std::min(kChunkSize, trace_data.size() - offset);
      buffer.Append(trace_data.data() + offset, sz);
      offset = (offset + sz) % trace_data.size();
      state.ResumeTiming();
    }
  }
  benchmark::DoNotOptimize(total_packet_size);
}

BENCHMARK(BM_ProtoRingBufferRead);
