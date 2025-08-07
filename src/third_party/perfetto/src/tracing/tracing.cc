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

#include "perfetto/tracing/tracing.h"

#include <atomic>
#include <condition_variable>
#include <mutex>

#include "perfetto/base/time.h"
#include "perfetto/ext/base/no_destructor.h"
#include "perfetto/ext/base/waitable_event.h"
#include "perfetto/tracing/internal/track_event_internal.h"
#include "src/tracing/internal/tracing_muxer_impl.h"

namespace perfetto {
namespace {
bool g_was_initialized = false;

// Wrapped in a function to avoid global constructor
std::mutex& InitializedMutex() {
  static base::NoDestructor<std::mutex> initialized_mutex;
  return initialized_mutex.ref();
}
}  // namespace

// static
void Tracing::InitializeInternal(const TracingInitArgs& args) {
  base::InitializeTime();
  std::unique_lock<std::mutex> lock(InitializedMutex());
  // If it's the first time Initialize is called, set some global params.
  if (!g_was_initialized) {
    // Make sure the headers and implementation files agree on the build config.
    PERFETTO_CHECK(args.dcheck_is_on_ == PERFETTO_DCHECK_IS_ON());
    if (args.log_message_callback) {
      base::SetLogMessageCallback(args.log_message_callback);
    }

    if (args.use_monotonic_clock) {
      PERFETTO_CHECK(!args.use_monotonic_raw_clock);
      internal::TrackEventInternal::SetClockId(
          protos::pbzero::BUILTIN_CLOCK_MONOTONIC);
    } else if (args.use_monotonic_raw_clock) {
      internal::TrackEventInternal::SetClockId(
          protos::pbzero::BUILTIN_CLOCK_MONOTONIC_RAW);
    }

    if (args.disallow_merging_with_system_tracks) {
      internal::TrackEventInternal::SetDisallowMergingWithSystemTracks(true);
    }
  }

  internal::TracingMuxerImpl::InitializeInstance(args);
  internal::TrackRegistry::InitializeInstance();
  g_was_initialized = true;
}

// static
bool Tracing::IsInitialized() {
  std::unique_lock<std::mutex> lock(InitializedMutex());
  return g_was_initialized;
}

// static
void Tracing::Shutdown() {
  std::unique_lock<std::mutex> lock(InitializedMutex());
  if (!g_was_initialized)
    return;
  internal::TracingMuxerImpl::Shutdown();
  g_was_initialized = false;
}

// static
void Tracing::ResetForTesting() {
  std::unique_lock<std::mutex> lock(InitializedMutex());
  if (!g_was_initialized)
    return;
  base::SetLogMessageCallback(nullptr);
  internal::TracingMuxerImpl::ResetForTesting();
  internal::TrackRegistry::ResetForTesting();
  g_was_initialized = false;
}

//  static
std::unique_ptr<TracingSession> Tracing::NewTraceInternal(
    BackendType backend,
    TracingConsumerBackend* (*system_backend_factory)()) {
  return static_cast<internal::TracingMuxerImpl*>(internal::TracingMuxer::Get())
      ->CreateTracingSession(backend, system_backend_factory);
}

//  static
std::unique_ptr<StartupTracingSession> Tracing::SetupStartupTracing(
    const TraceConfig& config,
    Tracing::SetupStartupTracingOpts opts) {
  return static_cast<internal::TracingMuxerImpl*>(internal::TracingMuxer::Get())
      ->CreateStartupTracingSession(config, std::move(opts));
}

//  static
std::unique_ptr<StartupTracingSession> Tracing::SetupStartupTracingBlocking(
    const TraceConfig& config,
    Tracing::SetupStartupTracingOpts opts) {
  return static_cast<internal::TracingMuxerImpl*>(internal::TracingMuxer::Get())
      ->CreateStartupTracingSessionBlocking(config, std::move(opts));
}

//  static
void Tracing::ActivateTriggers(const std::vector<std::string>& triggers,
                               uint32_t ttl_ms) {
  internal::TracingMuxer::Get()->ActivateTriggers(triggers, ttl_ms);
}

TracingSession::~TracingSession() = default;

void TracingSession::CloneTrace(CloneTraceArgs, CloneTraceCallback) {}

// Can be called from any thread.
bool TracingSession::FlushBlocking(uint32_t timeout_ms) {
  std::atomic<bool> flush_result;
  base::WaitableEvent flush_ack;

  // The non blocking Flush() can be called on any thread. It does the PostTask
  // internally.
  Flush(
      [&flush_ack, &flush_result](bool res) {
        flush_result = res;
        flush_ack.Notify();
      },
      timeout_ms);
  flush_ack.Wait();
  return flush_result;
}

std::vector<char> TracingSession::ReadTraceBlocking() {
  std::vector<char> raw_trace;
  std::mutex mutex;
  std::condition_variable cv;

  bool all_read = false;

  ReadTrace([&mutex, &raw_trace, &all_read, &cv](ReadTraceCallbackArgs cb) {
    raw_trace.insert(raw_trace.end(), cb.data, cb.data + cb.size);
    std::unique_lock<std::mutex> lock(mutex);
    all_read = !cb.has_more;
    if (all_read)
      cv.notify_one();
  });

  {
    std::unique_lock<std::mutex> lock(mutex);
    cv.wait(lock, [&all_read] { return all_read; });
  }
  return raw_trace;
}

TracingSession::GetTraceStatsCallbackArgs
TracingSession::GetTraceStatsBlocking() {
  std::mutex mutex;
  std::condition_variable cv;
  GetTraceStatsCallbackArgs result;
  bool stats_read = false;

  GetTraceStats(
      [&mutex, &result, &stats_read, &cv](GetTraceStatsCallbackArgs args) {
        result = std::move(args);
        std::unique_lock<std::mutex> lock(mutex);
        stats_read = true;
        cv.notify_one();
      });

  {
    std::unique_lock<std::mutex> lock(mutex);
    cv.wait(lock, [&stats_read] { return stats_read; });
  }
  return result;
}

TracingSession::QueryServiceStateCallbackArgs
TracingSession::QueryServiceStateBlocking() {
  std::mutex mutex;
  std::condition_variable cv;
  QueryServiceStateCallbackArgs result;
  bool status_read = false;

  QueryServiceState(
      [&mutex, &result, &status_read, &cv](QueryServiceStateCallbackArgs args) {
        result = std::move(args);
        std::unique_lock<std::mutex> lock(mutex);
        status_read = true;
        cv.notify_one();
      });

  {
    std::unique_lock<std::mutex> lock(mutex);
    cv.wait(lock, [&status_read] { return status_read; });
  }
  return result;
}

StartupTracingSession::~StartupTracingSession() = default;

}  // namespace perfetto
