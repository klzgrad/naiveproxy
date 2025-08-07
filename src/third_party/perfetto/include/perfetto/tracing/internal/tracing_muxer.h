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

#ifndef INCLUDE_PERFETTO_TRACING_INTERNAL_TRACING_MUXER_H_
#define INCLUDE_PERFETTO_TRACING_INTERNAL_TRACING_MUXER_H_

#include <atomic>
#include <memory>

#include "perfetto/base/export.h"
#include "perfetto/tracing/buffer_exhausted_policy.h"
#include "perfetto/tracing/core/forward_decls.h"
#include "perfetto/tracing/interceptor.h"
#include "perfetto/tracing/internal/basic_types.h"
#include "perfetto/tracing/internal/tracing_tls.h"
#include "perfetto/tracing/platform.h"

namespace perfetto {

class DataSourceBase;
class TraceWriterBase;
struct TracingInitArgs;
class TracingSession;

namespace internal {

struct DataSourceParams {
  // This is the policy configured by the data source code
  // (DataSource::kBufferExhaustedPolicy in C++ or
  // PerfettoDsSetBufferExhaustedPolicy in C). It can be overridden by the
  // consumer via config if buffer_exhausted_policy_configurable is true.
  BufferExhaustedPolicy default_buffer_exhausted_policy =
      BufferExhaustedPolicy::kDrop;
  bool buffer_exhausted_policy_configurable = false;
  bool supports_multiple_instances = true;
  bool requires_callbacks_under_lock = true;
};

struct DataSourceStaticState;

// This class acts as a bridge between the public API methods and the
// TracingBackend(s). It exposes a simplified view of the world to the API
// methods, so that they don't have to care about the multiplicity of backends.
// It handles all the bookkeeping to map data source instances and trace writers
// to the various backends.
// See tracing_muxer_impl.h for the full picture. This class contains only the
// fewer fields and methods that need to be exposed to public/ headers. Fields
// and methods that are required to implement them should go into
// src/tracing/internal/tracing_muxer_impl.h instead: that one can pull in
// perfetto headers outside of public, this one cannot.
class PERFETTO_EXPORT_COMPONENT TracingMuxer {
 public:
  static TracingMuxer* Get() { return instance_; }

  virtual ~TracingMuxer();

  TracingTLS* GetOrCreateTracingTLS() {
    return static_cast<TracingTLS*>(platform_->GetOrCreateThreadLocalObject());
  }

  // This method can fail and return false if trying to register more than
  // kMaxDataSources types.
  using DataSourceFactory = std::function<std::unique_ptr<DataSourceBase>()>;
  virtual bool RegisterDataSource(const DataSourceDescriptor&,
                                  DataSourceFactory,
                                  DataSourceParams,
                                  bool no_flush,
                                  DataSourceStaticState*) = 0;

  // Updates the DataSourceDescriptor for the DataSource.
  virtual void UpdateDataSourceDescriptor(const DataSourceDescriptor&,
                                          const DataSourceStaticState*) = 0;

  // It identifies the right backend and forwards the call to it.
  // The returned TraceWriter must be used within the same sequence (for most
  // projects this means "same thread"). Alternatively the client needs to take
  // care of using synchronization primitives to prevent concurrent accesses.
  virtual std::unique_ptr<TraceWriterBase> CreateTraceWriter(
      DataSourceStaticState*,
      uint32_t data_source_instance_index,
      DataSourceState*,
      BufferExhaustedPolicy buffer_exhausted_policy) = 0;

  virtual void DestroyStoppedTraceWritersForCurrentThread() = 0;

  uint32_t generation(std::memory_order ord) { return generation_.load(ord); }

  using InterceptorFactory = std::function<std::unique_ptr<InterceptorBase>()>;
  virtual void RegisterInterceptor(const InterceptorDescriptor&,
                                   InterceptorFactory,
                                   InterceptorBase::TLSFactory,
                                   InterceptorBase::TracePacketCallback) = 0;

  // Informs the tracing services to activate any of these triggers if any
  // tracing session was waiting for them.
  //
  // Sends the trigger signal to all the initialized backends that are currently
  // connected and that connect in the next `ttl_ms` milliseconds (but returns
  // immediately anyway).
  virtual void ActivateTriggers(const std::vector<std::string>&,
                                uint32_t ttl_ms) = 0;

  base::PlatformThreadId GetCurrentThreadId() {
    return platform_->GetCurrentThreadId();
  }

 protected:
  explicit TracingMuxer(Platform* platform) : platform_(platform) {}

  static TracingMuxer* instance_;
  Platform* const platform_ = nullptr;

  // Incremented every time a data source is destroyed. See tracing_tls.h.
  std::atomic<uint32_t> generation_{};
};

}  // namespace internal
}  // namespace perfetto

#endif  // INCLUDE_PERFETTO_TRACING_INTERNAL_TRACING_MUXER_H_
