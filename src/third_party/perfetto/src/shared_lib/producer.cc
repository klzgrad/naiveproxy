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

#include "perfetto/public/abi/producer_abi.h"

#include "perfetto/tracing/backend_type.h"
#include "perfetto/tracing/tracing.h"
#include "src/shared_lib/reset_for_testing.h"
#include "src/tracing/internal/tracing_muxer_impl.h"

namespace perfetto {
namespace shlib {

void ResetForTesting() {
  auto* muxer = static_cast<internal::TracingMuxerImpl*>(
      internal::TracingMuxerImpl::instance_);
  muxer->AppendResetForTestingCallback([] {
    perfetto::shlib::ResetDataSourceTls();
    perfetto::shlib::ResetTrackEventTls();
  });
  perfetto::Tracing::ResetForTesting();
}

}  // namespace shlib
}  // namespace perfetto

struct PerfettoProducerBackendInitArgs {
  uint32_t shmem_size_hint_kb = 0;
};

struct PerfettoProducerBackendInitArgs*
PerfettoProducerBackendInitArgsCreate() {
  return new PerfettoProducerBackendInitArgs();
}

void PerfettoProducerBackendInitArgsSetShmemSizeHintKb(
    struct PerfettoProducerBackendInitArgs* backend_args,
    uint32_t size) {
  backend_args->shmem_size_hint_kb = size;
}

void PerfettoProducerBackendInitArgsDestroy(
    struct PerfettoProducerBackendInitArgs* backend_args) {
  delete backend_args;
}

void PerfettoProducerInProcessInit(
    const struct PerfettoProducerBackendInitArgs* backend_args) {
  perfetto::TracingInitArgs args;
  args.backends = perfetto::kInProcessBackend;
  args.shmem_size_hint_kb = backend_args->shmem_size_hint_kb;
  perfetto::Tracing::Initialize(args);
}

void PerfettoProducerSystemInit(
    const struct PerfettoProducerBackendInitArgs* backend_args) {
  perfetto::TracingInitArgs args;
  args.backends = perfetto::kSystemBackend;
  args.shmem_size_hint_kb = backend_args->shmem_size_hint_kb;
  perfetto::Tracing::Initialize(args);
}

void PerfettoProducerActivateTriggers(const char* trigger_names[],
                                      uint32_t ttl_ms) {
  std::vector<std::string> triggers;
  for (size_t i = 0; trigger_names[i] != nullptr; i++) {
    triggers.push_back(trigger_names[i]);
  }
  perfetto::Tracing::ActivateTriggers(triggers, ttl_ms);
}
