// Copyright (C) 2025 The Android Open Source Project
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

#include <memory>
#include <random>
#include <vector>

#include "perfetto/base/time.h"
#include "perfetto/ext/tracing/core/client_identity.h"
#include "perfetto/ext/tracing/core/shared_memory_abi.h"
#include "perfetto/ext/tracing/core/trace_packet.h"
#include "src/tracing/service/trace_buffer.h"
#include "src/tracing/service/trace_buffer_v1.h"
#include "src/tracing/service/trace_buffer_v2.h"
#include "src/tracing/test/fake_packet.h"

namespace perfetto {
namespace {

constexpr size_t kChunkSize = 4096;

bool IsBenchmarkFunctionalOnly() {
  return getenv("BENCHMARK_FUNCTIONAL_TEST_ONLY") != nullptr;
}

static void BmArgs(benchmark::internal::Benchmark* b) {
  if (IsBenchmarkFunctionalOnly()) {
    b->Iterations(1);
  }
}

// Pre-generated chunk data to avoid measuring chunk generation time
struct ChunkTemplate {
  std::vector<uint8_t> data;
  uint16_t num_fragments;
  uint8_t flags;
};

// Generate a set of chunk templates with variable packet sizes
std::vector<ChunkTemplate> GenerateChunkTemplates(size_t num_templates) {
  std::vector<ChunkTemplate> templates;
  templates.reserve(num_templates);
  std::minstd_rand rnd(42);

  for (size_t i = 0; i < num_templates; ++i) {
    ChunkTemplate tmpl;
    tmpl.flags = 0;
    tmpl.num_fragments = 0;

    // Generate 5-15 packets per chunk with variable sizes
    size_t num_packets = 5 + (rnd() % 11);
    size_t bytes_used = 0;
    const size_t max_chunk_payload = kChunkSize - 16;  // Minus chunk header

    for (size_t p = 0; p < num_packets && bytes_used < max_chunk_payload - 50;
         ++p) {
      // Packet sizes between 50 and 500 bytes
      size_t packet_size = 50 + (rnd() % 451);
      if (bytes_used + packet_size > max_chunk_payload) {
        packet_size = max_chunk_payload - bytes_used;
      }
      if (packet_size < 10)
        break;

      char seed = static_cast<char>('a' + (i % 26));
      FakePacketFragment frag(packet_size, seed);
      frag.CopyInto(&tmpl.data);
      tmpl.num_fragments++;
      bytes_used += packet_size;
    }

    templates.push_back(std::move(tmpl));
  }

  return templates;
}

// Benchmark 1a: Write performance - Single writer
template <typename BufferType>
static void BM_TraceBuffer_WR_SingleWriter(benchmark::State& state) {
  constexpr size_t kBufferSize = 64 * 1024 * 1024;
  auto chunk_templates = GenerateChunkTemplates(100);

  auto buffer = BufferType::Create(kBufferSize);
  PERFETTO_CHECK(buffer);
  ClientIdentity client_identity(1000, 100);
  ChunkID chunk_id = 0;
  size_t template_idx = 0;
  size_t total_bytes_written = 0;

  for (auto _ : state) {
    size_t bytes_written = 0;
    while (bytes_written < kBufferSize) {
      const auto& tmpl = chunk_templates[template_idx % chunk_templates.size()];
      ++template_idx;
      benchmark::DoNotOptimize(tmpl.data.data());
      buffer->CopyChunkUntrusted(ProducerID(1), client_identity, WriterID(1),
                                 chunk_id++, tmpl.num_fragments, tmpl.flags,
                                 /*chunk_complete=*/true, tmpl.data.data(),
                                 tmpl.data.size());
      bytes_written += kChunkSize;
    }
    total_bytes_written += bytes_written;
  }

  TraceStats::BufferStats stats = buffer->stats();
  benchmark::DoNotOptimize(buffer);
  state.SetBytesProcessed(static_cast<int64_t>(total_bytes_written));
}

// Benchmark 1b: Write performance - Multiple writers
template <typename BufferType>
static void BM_TraceBuffer_WR_MultipleWriters(benchmark::State& state) {
  constexpr size_t kBufferSize = 64 * 1024 * 1024;
  constexpr size_t kNumWriters = 1000;

  // Pre-generate chunk templates OUTSIDE the benchmark loop
  auto chunk_templates = GenerateChunkTemplates(100);

  // Pre-generate client identities
  std::vector<ClientIdentity> client_identities;
  client_identities.reserve(kNumWriters);
  for (size_t i = 0; i < kNumWriters; ++i) {
    client_identities.emplace_back(1000 + i, 100 + i);
  }
  auto buffer = BufferType::Create(kBufferSize);
  PERFETTO_CHECK(buffer);
  size_t total_bytes_written = 0;
  size_t template_idx = 0;
  std::vector<ChunkID> chunk_ids(kNumWriters, 0);

  for (auto _ : state) {
    size_t bytes_written = 0;
    size_t writer_idx = 0;

    while (bytes_written < kBufferSize) {
      ProducerID producer_id = (writer_idx % kNumWriters) + 1;
      WriterID writer_id = (writer_idx % kNumWriters) + 1;
      const auto& tmpl = chunk_templates[template_idx % chunk_templates.size()];

      buffer->CopyChunkUntrusted(
          producer_id, client_identities[writer_idx % kNumWriters], writer_id,
          chunk_ids[writer_idx % kNumWriters]++, tmpl.num_fragments, tmpl.flags,
          /*chunk_complete=*/true, tmpl.data.data(), tmpl.data.size());

      bytes_written += kChunkSize;
      writer_idx++;
      template_idx++;
    }
    total_bytes_written += bytes_written;
  }

  state.SetBytesProcessed(static_cast<int64_t>(total_bytes_written));
}

// Benchmark 2: Read performance with mixed standalone and fragmented packets
template <typename BufferType>
static void BM_TraceBuffer_RD(benchmark::State& state) {
  constexpr size_t kBufferSize = 128 * 1024 * 1024;

  // Pre-generate chunk templates with fragmented packets OUTSIDE the loop
  // auto chunk_templates = GenerateFragmentedChunkTemplates();
  auto chunk_templates = GenerateChunkTemplates(100);

  ClientIdentity client_identity(1000, 100);

  size_t total_bytes_read = 0;

  for (auto _ : state) {
    // Pause timing while we populate the buffer (setup phase)
    // We cannot populate the buffer outside, because read is consuming, and
    // after the first read we'd just return an empty buffer.
    state.PauseTiming();

    // Create and populate the buffer for this iteration
    auto buffer = BufferType::Create(kBufferSize);
    PERFETTO_CHECK(buffer);

    ChunkID chunk_id = 0;
    size_t bytes_written = 0;
    size_t template_idx = 0;

    while (bytes_written < kBufferSize - kChunkSize) {
      const auto& tmpl = chunk_templates[template_idx % chunk_templates.size()];

      buffer->CopyChunkUntrusted(ProducerID(1), client_identity, WriterID(1),
                                 chunk_id++, tmpl.num_fragments, tmpl.flags,
                                 /*chunk_complete=*/true, tmpl.data.data(),
                                 tmpl.data.size());

      bytes_written += kChunkSize;
      template_idx++;
    }

    // Resume timing for the actual read benchmark
    state.ResumeTiming();

    // Now benchmark reading
    TraceBuffer::PacketSequenceProperties seq_props;
    bool packet_dropped;
    size_t bytes_read = 0;

    buffer->BeginRead();
    for (;;) {
      TracePacket packet;
      if (!buffer->ReadNextTracePacket(&packet, &seq_props, &packet_dropped)) {
        break;
      }
      for (const auto& slice : packet.slices()) {
        bytes_read += slice.size;
      }
    }

    total_bytes_read += bytes_read;
  }

  state.SetBytesProcessed(static_cast<int64_t>(total_bytes_read));
}

// Instantiate benchmarks for both V1 and V2

// Write benchmarks - Single writer
BENCHMARK_TEMPLATE(BM_TraceBuffer_WR_SingleWriter, TraceBufferV1)
    ->Apply(BmArgs);
BENCHMARK_TEMPLATE(BM_TraceBuffer_WR_SingleWriter, TraceBufferV2)
    ->Apply(BmArgs);

// Write benchmarks - Multiple writers
BENCHMARK_TEMPLATE(BM_TraceBuffer_WR_MultipleWriters, TraceBufferV1)
    ->Apply(BmArgs);
BENCHMARK_TEMPLATE(BM_TraceBuffer_WR_MultipleWriters, TraceBufferV2)
    ->Apply(BmArgs);

// Read benchmarks
BENCHMARK_TEMPLATE(BM_TraceBuffer_RD, TraceBufferV1)->Apply(BmArgs);
BENCHMARK_TEMPLATE(BM_TraceBuffer_RD, TraceBufferV2)->Apply(BmArgs);

}  // namespace
}  // namespace perfetto
