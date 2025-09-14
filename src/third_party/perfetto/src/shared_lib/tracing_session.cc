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

#include "perfetto/public/abi/tracing_session_abi.h"

#include <condition_variable>
#include <mutex>

#include "perfetto/tracing/tracing.h"
#include "protos/perfetto/config/trace_config.gen.h"

struct PerfettoTracingSessionImpl* PerfettoTracingSessionSystemCreate() {
  std::unique_ptr<perfetto::TracingSession> tracing_session =
      perfetto::Tracing::NewTrace(perfetto::kSystemBackend);
  return reinterpret_cast<struct PerfettoTracingSessionImpl*>(
      tracing_session.release());
}

struct PerfettoTracingSessionImpl* PerfettoTracingSessionInProcessCreate() {
  std::unique_ptr<perfetto::TracingSession> tracing_session =
      perfetto::Tracing::NewTrace(perfetto::kInProcessBackend);
  return reinterpret_cast<struct PerfettoTracingSessionImpl*>(
      tracing_session.release());
}

void PerfettoTracingSessionSetup(struct PerfettoTracingSessionImpl* session,
                                 void* cfg_begin,
                                 size_t cfg_len) {
  auto* ts = reinterpret_cast<perfetto::TracingSession*>(session);
  perfetto::TraceConfig cfg;
  cfg.ParseFromArray(cfg_begin, cfg_len);
  ts->Setup(cfg);
}

void PerfettoTracingSessionSetStopCb(struct PerfettoTracingSessionImpl* session,
                                     PerfettoTracingSessionStopCb cb,
                                     void* user_arg) {
  auto* ts = reinterpret_cast<perfetto::TracingSession*>(session);
  ts->SetOnStopCallback([session, cb, user_arg]() { cb(session, user_arg); });
}

void PerfettoTracingSessionStartAsync(
    struct PerfettoTracingSessionImpl* session) {
  auto* ts = reinterpret_cast<perfetto::TracingSession*>(session);
  ts->Start();
}

void PerfettoTracingSessionStartBlocking(
    struct PerfettoTracingSessionImpl* session) {
  auto* ts = reinterpret_cast<perfetto::TracingSession*>(session);
  ts->StartBlocking();
}

void PerfettoTracingSessionFlushAsync(
    struct PerfettoTracingSessionImpl* session,
    uint32_t timeout_ms,
    PerfettoTracingSessionFlushCb cb,
    void* user_arg) {
  auto* ts = reinterpret_cast<perfetto::TracingSession*>(session);
  std::function<void(bool)> flush_cb = [](bool) {};
  if (cb) {
    flush_cb = [cb, session, user_arg](bool success) {
      cb(session, success, user_arg);
    };
  }
  ts->Flush(std::move(flush_cb), timeout_ms);
}

bool PerfettoTracingSessionFlushBlocking(
    struct PerfettoTracingSessionImpl* session,
    uint32_t timeout_ms) {
  auto* ts = reinterpret_cast<perfetto::TracingSession*>(session);
  return ts->FlushBlocking(timeout_ms);
}

void PerfettoTracingSessionStopAsync(
    struct PerfettoTracingSessionImpl* session) {
  auto* ts = reinterpret_cast<perfetto::TracingSession*>(session);
  ts->Stop();
}

void PerfettoTracingSessionStopBlocking(
    struct PerfettoTracingSessionImpl* session) {
  auto* ts = reinterpret_cast<perfetto::TracingSession*>(session);
  ts->StopBlocking();
}

void PerfettoTracingSessionReadTraceBlocking(
    struct PerfettoTracingSessionImpl* session,
    PerfettoTracingSessionReadCb callback,
    void* user_arg) {
  auto* ts = reinterpret_cast<perfetto::TracingSession*>(session);

  std::mutex mutex;
  std::condition_variable cv;

  bool all_read = false;

  ts->ReadTrace([&mutex, &all_read, &cv, session, callback, user_arg](
                    perfetto::TracingSession::ReadTraceCallbackArgs args) {
    callback(session, static_cast<const void*>(args.data), args.size,
             args.has_more, user_arg);
    std::unique_lock<std::mutex> lock(mutex);
    all_read = !args.has_more;
    if (all_read)
      cv.notify_one();
  });

  {
    std::unique_lock<std::mutex> lock(mutex);
    cv.wait(lock, [&all_read] { return all_read; });
  }
}

void PerfettoTracingSessionDestroy(struct PerfettoTracingSessionImpl* session) {
  auto* ts = reinterpret_cast<perfetto::TracingSession*>(session);
  delete ts;
}
