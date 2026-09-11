/*
 * Copyright (C) 2019 The Android Open Source Project
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

#include "src/tracing/test/api_test_support.h"

#include "perfetto/base/compiler.h"
#include "perfetto/base/proc_utils.h"
#include "perfetto/base/time.h"
#include "perfetto/ext/base/string_utils.h"
#include "perfetto/ext/base/temp_file.h"
#include "perfetto/tracing/internal/basic_types.h"
#include "src/tracing/internal/tracing_muxer_impl.h"

#include <sstream>

#if PERFETTO_BUILDFLAG(PERFETTO_IPC)
#include "test/test_helper.h"
#endif

#if PERFETTO_BUILDFLAG(PERFETTO_OS_WIN)
#include <windows.h>
#endif

namespace perfetto {
namespace test {

using internal::TracingMuxerImpl;

#if PERFETTO_BUILDFLAG(PERFETTO_IPC)
namespace {

class InProcessSystemService {
 public:
  InProcessSystemService()
      : test_helper_(&task_runner_, TestHelper::Mode::kStartDaemons) {
    // Will always start service because we explicitly set kStartDaemons.
    test_helper_.StartServiceIfRequired();
  }

  void CleanEnv() { test_helper_.CleanEnv(); }

  void Restart() { test_helper_.RestartService(); }

 private:
  perfetto::base::TestTaskRunner task_runner_;
  perfetto::TestHelper test_helper_;
};

InProcessSystemService* g_system_service = nullptr;

}  // namespace

// static
SystemService SystemService::Start() {
  // If there already was a system service running, make sure the new one is
  // running before tearing down the old one. This avoids a 1 second
  // reconnection delay between each test since the connection to the new
  // service succeeds immediately.
  std::unique_ptr<InProcessSystemService> old_service(g_system_service);
  if (old_service) {
    old_service->CleanEnv();
  }
  g_system_service = new InProcessSystemService();

  // Tear down the service at process exit to make sure temporary files get
  // deleted.
  static bool cleanup_registered;
  if (!cleanup_registered) {
    atexit([] { delete g_system_service; });
    cleanup_registered = true;
  }
  SystemService ret;
  ret.valid_ = true;
  return ret;
}

void SystemService::Clean() {
  if (valid_) {
    if (g_system_service) {
      // Does not really stop. We want to reuse the service in future tests. It
      // is important to clean the environment variables, though.
      g_system_service->CleanEnv();
    }
  }
  valid_ = false;
}

void SystemService::Restart() {
  PERFETTO_CHECK(valid_);
  g_system_service->Restart();
}
#else   // !PERFETTO_BUILDFLAG(PERFETTO_IPC)
// static
SystemService SystemService::Start() {
  return SystemService();
}
void SystemService::Clean() {
  valid_ = false;
}
void SystemService::Restart() {
  valid_ = false;
}
#endif  // !PERFETTO_BUILDFLAG(PERFETTO_IPC)

SystemService& SystemService::operator=(SystemService&& other) noexcept {
  PERFETTO_CHECK(!valid_ || !other.valid_);
  Clean();
  valid_ = other.valid_;
  other.valid_ = false;
  return *this;
}

int32_t GetCurrentProcessId() {
  return static_cast<int32_t>(base::GetProcessId());
}

void SyncProducers() {
  auto* muxer = reinterpret_cast<perfetto::internal::TracingMuxerImpl*>(
      perfetto::internal::TracingMuxer::Get());
  muxer->SyncProducersForTesting();
}

void SetBatchCommitsDuration(uint32_t batch_commits_duration_ms,
                             BackendType backend_type) {
  auto* muxer = reinterpret_cast<perfetto::internal::TracingMuxerImpl*>(
      perfetto::internal::TracingMuxer::Get());
  muxer->SetBatchCommitsDurationForTesting(batch_commits_duration_ms,
                                           backend_type);
}

void DisableReconnectLimit() {
  auto* muxer = reinterpret_cast<perfetto::internal::TracingMuxerImpl*>(
      perfetto::internal::TracingMuxer::Get());
  muxer->SetMaxProducerReconnectionsForTesting(
      std::numeric_limits<uint32_t>::max());
}

bool EnableDirectSMBPatching(BackendType backend_type) {
  auto* muxer = reinterpret_cast<perfetto::internal::TracingMuxerImpl*>(
      perfetto::internal::TracingMuxer::Get());
  return muxer->EnableDirectSMBPatchingForTesting(backend_type);
}

TestTempFile CreateTempFile() {
  TestTempFile res{};
#if PERFETTO_BUILDFLAG(PERFETTO_OS_WIN)
  base::StackString<255> temp_file("%s\\perfetto-XXXXXX", getenv("TMP"));
  PERFETTO_CHECK(_mktemp_s(temp_file.mutable_data(), temp_file.len() + 1) == 0);
  HANDLE handle =
      ::CreateFileA(temp_file.c_str(), GENERIC_READ | GENERIC_WRITE,
                    FILE_SHARE_DELETE | FILE_SHARE_READ, nullptr, CREATE_ALWAYS,
                    FILE_ATTRIBUTE_TEMPORARY, nullptr);
  PERFETTO_CHECK(handle && handle != INVALID_HANDLE_VALUE);
  res.fd = _open_osfhandle(reinterpret_cast<intptr_t>(handle), 0);
  res.path = temp_file.ToStdString();
#elif PERFETTO_BUILDFLAG(PERFETTO_OS_ANDROID)
  char temp_file[] = "/data/local/tmp/perfetto-XXXXXXXX";
  res.fd = mkstemp(temp_file);
  res.path = temp_file;
#else
  char temp_file[] = "/tmp/perfetto-XXXXXXXX";
  res.fd = mkstemp(temp_file);
  res.path = temp_file;
#endif
  PERFETTO_CHECK(res.fd > 0);
  return res;
}

// static
bool TracingMuxerImplInternalsForTest::DoesSystemBackendHaveSMB() {
  using RegisteredProducerBackend = TracingMuxerImpl::RegisteredProducerBackend;
  // Ideally we should be doing dynamic_cast and a DCHECK(muxer != nullptr);
  auto* muxer =
      reinterpret_cast<TracingMuxerImpl*>(TracingMuxerImpl::instance_);
  const auto& backends = muxer->producer_backends_;
  const auto& backend =
      std::find_if(backends.begin(), backends.end(),
                   [](const RegisteredProducerBackend& r_backend) {
                     return r_backend.type == kSystemBackend;
                   });
  if (backend == backends.end())
    return false;
  const auto& service = backend->producer->service_;
  return service && service->shared_memory();
}

// static
void TracingMuxerImplInternalsForTest::ClearIncrementalState() {
  auto* muxer =
      reinterpret_cast<TracingMuxerImpl*>(TracingMuxerImpl::instance_);
  for (const auto& data_source : muxer->data_sources_) {
    for (uint32_t inst_idx = 0; inst_idx < internal::kMaxDataSourceInstances;
         inst_idx++) {
      internal::DataSourceState* ds_static_state =
          data_source.static_state->TryGet(inst_idx);
      if (ds_static_state) {
        ds_static_state->incremental_state_generation.fetch_add(
            1, std::memory_order_relaxed);
      }
    }
  }
}

// static
void TracingMuxerImplInternalsForTest::AppendResetForTestingCallback(
    std::function<void()> f) {
  auto* muxer =
      reinterpret_cast<TracingMuxerImpl*>(TracingMuxerImpl::instance_);
  muxer->AppendResetForTestingCallback(f);
}

}  // namespace test
}  // namespace perfetto
