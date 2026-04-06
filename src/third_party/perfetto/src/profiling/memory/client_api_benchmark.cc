// Copyright (C) 2021 The Android Open Source Project
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

#include "perfetto/heap_profile.h"
#include "src/profiling/memory/heap_profile_internal.h"

#include "src/profiling/memory/client.h"
#include "src/profiling/memory/client_api_factory.h"

namespace perfetto {
namespace profiling {

namespace {
uint32_t GetHeapId() {
  static uint32_t heap_id =
      AHeapProfile_registerHeap(AHeapInfo_create("dev.perfetto.benchmark"));
  return heap_id;
}

ClientConfiguration g_client_config;
int g_shmem_fd;

base::UnixSocketRaw& GlobalServerSocket() {
  static base::UnixSocketRaw* srv_sock = new base::UnixSocketRaw;
  return *srv_sock;
}

void DisconnectGlobalServerSocket() {
  base::UnixSocketRaw destroy;
  std::swap(destroy, GlobalServerSocket());
}

}  // namespace

// This is called by AHeapProfile_initSession (client_api.cc) to construct a
// client. The Client API requires to be linked against another compliation
// unit that provides this function. This way, it can be used in different
// circumstances (central heapprofd, fork heapprofd) and be agnostic about the
// details. This is is used to create a test Client here.
void StartHeapprofdIfStatic() {}
std::shared_ptr<Client> ConstructClient(
    UnhookedAllocator<perfetto::profiling::Client> unhooked_allocator) {
  base::UnixSocketRaw cli_sock;
  base::UnixSocketRaw& srv_sock = GlobalServerSocket();
  std::tie(cli_sock, srv_sock) = base::UnixSocketRaw::CreatePairPosix(
      base::SockFamily::kUnix, base::SockType::kStream);
  auto ringbuf = SharedRingBuffer::Create(8 * 1048576);
  ringbuf->InfiniteBufferForTesting();
  PERFETTO_CHECK(ringbuf);
  PERFETTO_CHECK(cli_sock);
  PERFETTO_CHECK(srv_sock);
  g_shmem_fd = ringbuf->fd();
  return std::allocate_shared<Client>(unhooked_allocator, std::move(cli_sock),
                                      g_client_config, std::move(*ringbuf),
                                      getpid(), GetMainThreadStackRange());
}

static void BM_ClientApiOneTenthAllocation(benchmark::State& state) {
  const uint32_t heap_id = GetHeapId();

  ClientConfiguration client_config{};
  client_config.default_interval = 32000;
  client_config.all_heaps = true;
  g_client_config = client_config;
  PERFETTO_CHECK(AHeapProfile_initSession(malloc, free));

  PERFETTO_CHECK(g_shmem_fd);
  auto ringbuf = SharedRingBuffer::Attach(base::ScopedFile(dup(g_shmem_fd)));

  for (auto _ : state) {
    AHeapProfile_reportAllocation(heap_id, 0x123, 3200);
  }
  DisconnectGlobalServerSocket();
  ringbuf->SetShuttingDown();
}

BENCHMARK(BM_ClientApiOneTenthAllocation);

static void BM_ClientApiOneHundrethAllocation(benchmark::State& state) {
  const uint32_t heap_id = GetHeapId();

  ClientConfiguration client_config{};
  client_config.default_interval = 32000;
  client_config.all_heaps = true;
  g_client_config = client_config;
  PERFETTO_CHECK(AHeapProfile_initSession(malloc, free));

  PERFETTO_CHECK(g_shmem_fd);
  auto ringbuf = SharedRingBuffer::Attach(base::ScopedFile(dup(g_shmem_fd)));

  for (auto _ : state) {
    AHeapProfile_reportAllocation(heap_id, 0x123, 320);
  }
  DisconnectGlobalServerSocket();
  ringbuf->SetShuttingDown();
}

BENCHMARK(BM_ClientApiOneHundrethAllocation);

static void BM_ClientApiAlmostNoAllocation(benchmark::State& state) {
  const uint32_t heap_id = GetHeapId();

  ClientConfiguration client_config{};
  client_config.default_interval = 10000000000000000;
  client_config.all_heaps = true;
  g_client_config = client_config;
  PERFETTO_CHECK(AHeapProfile_initSession(malloc, free));

  PERFETTO_CHECK(g_shmem_fd);
  auto ringbuf = SharedRingBuffer::Attach(base::ScopedFile(dup(g_shmem_fd)));

  for (auto _ : state) {
    AHeapProfile_reportAllocation(heap_id, 0x123, 1);
  }
  DisconnectGlobalServerSocket();
  ringbuf->SetShuttingDown();
}

BENCHMARK(BM_ClientApiAlmostNoAllocation);

static void BM_ClientApiSample(benchmark::State& state) {
  const uint32_t heap_id = GetHeapId();

  ClientConfiguration client_config{};
  client_config.default_interval = 32000;
  client_config.all_heaps = true;
  g_client_config = client_config;
  PERFETTO_CHECK(AHeapProfile_initSession(malloc, free));

  PERFETTO_CHECK(g_shmem_fd);
  auto ringbuf = SharedRingBuffer::Attach(base::ScopedFile(dup(g_shmem_fd)));

  for (auto _ : state) {
    AHeapProfile_reportSample(heap_id, 0x123, 20);
  }
  DisconnectGlobalServerSocket();
  ringbuf->SetShuttingDown();
}

BENCHMARK(BM_ClientApiSample);

static void BM_ClientApiDisabledHeapAllocation(benchmark::State& state) {
  const uint32_t heap_id = GetHeapId();

  ClientConfiguration client_config{};
  client_config.default_interval = 32000;
  client_config.all_heaps = false;
  g_client_config = client_config;
  PERFETTO_CHECK(AHeapProfile_initSession(malloc, free));

  PERFETTO_CHECK(g_shmem_fd);
  auto ringbuf = SharedRingBuffer::Attach(base::ScopedFile(dup(g_shmem_fd)));

  for (auto _ : state) {
    AHeapProfile_reportAllocation(heap_id, 0x123, 20);
  }
  DisconnectGlobalServerSocket();
  ringbuf->SetShuttingDown();
}

BENCHMARK(BM_ClientApiDisabledHeapAllocation);

static void BM_ClientApiDisabledHeapFree(benchmark::State& state) {
  const uint32_t heap_id = GetHeapId();

  ClientConfiguration client_config{};
  client_config.default_interval = 32000;
  client_config.all_heaps = false;
  g_client_config = client_config;
  PERFETTO_CHECK(AHeapProfile_initSession(malloc, free));

  PERFETTO_CHECK(g_shmem_fd);
  auto ringbuf = SharedRingBuffer::Attach(base::ScopedFile(dup(g_shmem_fd)));

  for (auto _ : state) {
    AHeapProfile_reportFree(heap_id, 0x123);
  }
  DisconnectGlobalServerSocket();
  ringbuf->SetShuttingDown();
}

BENCHMARK(BM_ClientApiDisabledHeapFree);

static void BM_ClientApiEnabledHeapFree(benchmark::State& state) {
  const uint32_t heap_id = GetHeapId();

  ClientConfiguration client_config{};
  client_config.default_interval = 32000;
  client_config.all_heaps = true;
  g_client_config = client_config;
  PERFETTO_CHECK(AHeapProfile_initSession(malloc, free));

  PERFETTO_CHECK(g_shmem_fd);
  auto ringbuf = SharedRingBuffer::Attach(base::ScopedFile(dup(g_shmem_fd)));

  for (auto _ : state) {
    AHeapProfile_reportFree(heap_id, 0x123);
  }
  DisconnectGlobalServerSocket();
  ringbuf->SetShuttingDown();
}

BENCHMARK(BM_ClientApiEnabledHeapFree);

static void BM_ClientApiMallocFree(benchmark::State& state) {
  for (auto _ : state) {
    volatile char* x = static_cast<char*>(malloc(100));
    if (x) {
      x[0] = 'x';
      free(const_cast<char*>(x));
    }
  }
}

BENCHMARK(BM_ClientApiMallocFree);

}  // namespace profiling
}  // namespace perfetto
