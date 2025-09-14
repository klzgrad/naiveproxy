/*
 * Copyright (C) 2025 The Android Open Source Project
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

#include "src/java_sdk/main/cpp/utils.h"

#include <functional>

#include "perfetto/public/abi/heap_buffer.h"
#include "perfetto/public/abi/tracing_session_abi.h"
#include "perfetto/public/pb_msg.h"
#include "perfetto/public/protos/config/data_source_config.pzc.h"
#include "perfetto/public/protos/config/trace_config.pzc.h"
#include "perfetto/public/protos/config/track_event/track_event_config.pzc.h"

// Implementation copied from src/shared_lib/test/utils.cc

namespace perfetto {
namespace java_sdk {
namespace utils {
TracingSession TracingSession::Builder::Build() {
  struct PerfettoPbMsgWriter writer;
  struct PerfettoHeapBuffer* hb = PerfettoHeapBufferCreate(&writer.writer);

  struct perfetto_protos_TraceConfig cfg;
  PerfettoPbMsgInit(&cfg.msg, &writer);
  {
    struct perfetto_protos_TraceConfig_BufferConfig buffers;
    perfetto_protos_TraceConfig_begin_buffers(&cfg, &buffers);

    perfetto_protos_TraceConfig_BufferConfig_set_size_kb(&buffers, 1024);

    perfetto_protos_TraceConfig_end_buffers(&cfg, &buffers);
  }
  {
    struct perfetto_protos_TraceConfig_DataSource data_sources;
    perfetto_protos_TraceConfig_begin_data_sources(&cfg, &data_sources);
    {
      struct perfetto_protos_DataSourceConfig ds_cfg;
      perfetto_protos_TraceConfig_DataSource_begin_config(&data_sources,
                                                          &ds_cfg);

      perfetto_protos_DataSourceConfig_set_cstr_name(&ds_cfg,
                                                     data_source_name_.c_str());
      if (!enabled_categories_.empty() || !disabled_categories_.empty()) {
        perfetto_protos_TrackEventConfig te_cfg;
        perfetto_protos_DataSourceConfig_begin_track_event_config(&ds_cfg,
                                                                  &te_cfg);
        for (const std::string& cat : enabled_categories_) {
          perfetto_protos_TrackEventConfig_set_enabled_categories(
              &te_cfg, cat.data(), cat.size());
        }
        for (const std::string& cat : disabled_categories_) {
          perfetto_protos_TrackEventConfig_set_disabled_categories(
              &te_cfg, cat.data(), cat.size());
        }
        perfetto_protos_DataSourceConfig_end_track_event_config(&ds_cfg,
                                                                &te_cfg);
      }

      perfetto_protos_TraceConfig_DataSource_end_config(&data_sources, &ds_cfg);
    }

    perfetto_protos_TraceConfig_end_data_sources(&cfg, &data_sources);
  }
  size_t cfg_size = PerfettoStreamWriterGetWrittenSize(&writer.writer);
  std::unique_ptr<uint8_t[]> ser(new uint8_t[cfg_size]);
  PerfettoHeapBufferCopyInto(hb, &writer.writer, ser.get(), cfg_size);
  PerfettoHeapBufferDestroy(hb, &writer.writer);

  struct PerfettoTracingSessionImpl* ts =
      PerfettoTracingSessionInProcessCreate();

  PerfettoTracingSessionSetup(ts, ser.get(), cfg_size);

  PerfettoTracingSessionStartBlocking(ts);

  return TracingSession::Adopt(ts);
}

TracingSession TracingSession::Adopt(
    struct PerfettoTracingSessionImpl* session) {
  TracingSession ret;
  ret.session_ = session;
  ret.stopped_ = std::make_unique<WaitableEvent>();
  PerfettoTracingSessionSetStopCb(
      ret.session_,
      [](struct PerfettoTracingSessionImpl*, void* arg) {
        static_cast<WaitableEvent*>(arg)->Notify();
      },
      ret.stopped_.get());
  return ret;
}

TracingSession::TracingSession(TracingSession&& other) noexcept {
  session_ = other.session_;
  other.session_ = nullptr;
  stopped_ = std::move(other.stopped_);
  other.stopped_ = nullptr;
}

TracingSession::~TracingSession() {
  if (!session_) {
    return;
  }
  if (!stopped_->IsNotified()) {
    PerfettoTracingSessionStopBlocking(session_);
    stopped_->WaitForNotification();
  }
  PerfettoTracingSessionDestroy(session_);
}

bool TracingSession::FlushBlocking(uint32_t timeout_ms) {
  WaitableEvent notification;
  bool result;
  auto* cb = new std::function<void(bool)>([&](bool success) {
    result = success;
    notification.Notify();
  });
  PerfettoTracingSessionFlushAsync(
      session_, timeout_ms,
      [](PerfettoTracingSessionImpl*, bool success, void* user_arg) {
        auto* f = reinterpret_cast<std::function<void(bool)>*>(user_arg);
        (*f)(success);
        delete f;
      },
      cb);
  notification.WaitForNotification();
  return result;
}

void TracingSession::WaitForStopped() {
  stopped_->WaitForNotification();
}

void TracingSession::StopAsync() {
  PerfettoTracingSessionStopAsync(session_);
}

void TracingSession::StopBlocking() {
  PerfettoTracingSessionStopBlocking(session_);
}

std::vector<uint8_t> TracingSession::ReadBlocking() {
  std::vector<uint8_t> data;
  PerfettoTracingSessionReadTraceBlocking(
      session_,
      [](struct PerfettoTracingSessionImpl*, const void* trace_data,
         size_t size, bool, void* user_arg) {
        auto& dst = *static_cast<std::vector<uint8_t>*>(user_arg);
        auto* src = static_cast<const uint8_t*>(trace_data);
        dst.insert(dst.end(), src, src + size);
      },
      &data);
  return data;
}
}  // namespace utils
}  // namespace java_sdk
}  // namespace perfetto
