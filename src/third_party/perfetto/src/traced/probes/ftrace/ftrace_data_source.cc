/*
 * Copyright (C) 2018 The Android Open Source Project
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

#include "src/traced/probes/ftrace/ftrace_data_source.h"

#include "perfetto/ext/base/string_splitter.h"
#include "perfetto/ext/base/string_utils.h"
#include "perfetto/ext/base/string_view.h"
#include "perfetto/ext/base/subprocess.h"
#include "perfetto/protozero/scattered_heap_buffer.h"
#include "perfetto/tracing/core/data_source_descriptor.h"
#include "src/traced/probes/ftrace/cpu_reader.h"
#include "src/traced/probes/ftrace/ftrace_controller.h"

#include "protos/perfetto/common/ftrace_descriptor.pbzero.h"
#include "protos/perfetto/trace/ftrace/ftrace_stats.pbzero.h"
#include "protos/perfetto/trace/trace_packet.pbzero.h"

namespace perfetto {
namespace {

void FillFtraceDataSourceDescriptor(DataSourceDescriptor* dsd) {
  protozero::HeapBuffered<protos::pbzero::FtraceDescriptor> ftd;

#if PERFETTO_BUILDFLAG(PERFETTO_OS_ANDROID)
  base::Subprocess p({"/system/bin/atrace", "--list_categories"});
  p.args.stdin_mode = base::Subprocess::InputMode::kDevNull;
  p.args.stdout_mode = base::Subprocess::OutputMode::kBuffer;
  p.args.stderr_mode = base::Subprocess::OutputMode::kBuffer;
  bool res = p.Call(/*timeout_ms=*/20000);
  if (res) {
    for (base::StringSplitter ss(std::move(p.output()), '\n'); ss.Next();) {
      base::StringView line(ss.cur_token(), ss.cur_token_size());
      size_t pos = line.find(" - ");
      if (pos == line.npos) {
        continue;
      }
      base::StringView name = line.substr(0, pos);
      // Trim initial whitespaces
      auto it = std::find_if(name.begin(), name.end(),
                             [](char c) { return c != ' '; });
      name = name.substr(static_cast<size_t>(it - name.begin()));

      base::StringView desc = line.substr(pos + 3);

      protos::pbzero::FtraceDescriptor::AtraceCategory* cat =
          ftd->add_atrace_categories();
      cat->set_name(name.data(), name.size());
      cat->set_description(desc.data(), desc.size());
    }
  } else {
    PERFETTO_ELOG("Failed to run atrace --list_categories code(%d): %s",
                  p.returncode(), p.output().c_str());
  }
#endif

  dsd->set_ftrace_descriptor_raw(ftd.SerializeAsString());
}

}  // namespace

// static
const ProbesDataSource::Descriptor FtraceDataSource::descriptor = {
    /*name*/ "linux.ftrace",
    /*flags*/ Descriptor::kFlagsNone,
    /*fill_descriptor_func*/ &FillFtraceDataSourceDescriptor,
};

FtraceDataSource::FtraceDataSource(
    base::WeakPtr<FtraceController> controller_weak,
    TracingSessionID session_id,
    FtraceConfig config,
    std::unique_ptr<TraceWriter> writer)
    : ProbesDataSource(session_id, &descriptor),
      config_(std::move(config)),
      writer_(std::move(writer)),
      controller_weak_(std::move(controller_weak)) {}

FtraceDataSource::~FtraceDataSource() {
  if (controller_weak_)
    controller_weak_->RemoveDataSource(this);
}

void FtraceDataSource::Initialize(
    FtraceConfigId config_id,
    const FtraceDataSourceConfig* parsing_config) {
  PERFETTO_CHECK(config_id);
  config_id_ = config_id;
  parsing_config_ = parsing_config;
}

void FtraceDataSource::Start() {
  if (!controller_weak_)
    return;

  PERFETTO_CHECK(config_id_);
  if (!controller_weak_->StartDataSource(this))
    return;

  // Note: recording is already active by this point, so the buffer stats are
  // likely already non-zero even if this is the only ftrace data source.
  controller_weak_->DumpFtraceStats(this, &stats_before_);

  // If serialising pre-existing ftrace data, emit a special packet so that
  // trace_processor doesn't filter out data before start-of-trace.
  if (config_.preserve_ftrace_buffer()) {
    auto stats_packet = writer_->NewTracePacket();
    auto* stats = stats_packet->set_ftrace_stats();
    stats->set_phase(protos::pbzero::FtraceStats::Phase::START_OF_TRACE);
    stats->set_preserve_ftrace_buffer(true);
  }
}

void FtraceDataSource::Flush(FlushRequestID flush_request_id,
                             std::function<void()> callback) {
  if (!controller_weak_)
    return;

  pending_flushes_[flush_request_id] = std::move(callback);

  // FtraceController will call OnFtraceFlushComplete() once the data has been
  // drained from the ftrace buffer and written into the various writers of
  // all its active data sources.
  controller_weak_->Flush(flush_request_id);
}

// Called by FtraceController after all CPUs have acked the flush or timed out.
void FtraceDataSource::OnFtraceFlushComplete(FlushRequestID flush_request_id) {
  auto it = pending_flushes_.find(flush_request_id);
  if (it == pending_flushes_.end()) {
    // This can genuinely happen in case of concurrent ftrace sessions. When a
    // FtraceDataSource issues a flush, the controller has to drain ftrace data
    // for everybody (there is only one kernel ftrace buffer for all sessions).
    // FtraceController doesn't bother to remember which FtraceDataSource did or
    // did not request a flush. Instead just boradcasts the
    // OnFtraceFlushComplete() to all of them.
    return;
  }
  auto callback = std::move(it->second);
  pending_flushes_.erase(it);
  if (writer_) {
    WriteStats();
    writer_->Flush(std::move(callback));
  }
}

void FtraceDataSource::WriteStats() {
  if (!controller_weak_) {
    return;
  }
  {
    auto before_packet = writer_->NewTracePacket();
    auto out = before_packet->set_ftrace_stats();
    out->set_phase(protos::pbzero::FtraceStats::Phase::START_OF_TRACE);
    stats_before_.Write(out);
  }
  {
    FtraceStats stats_after{};
    controller_weak_->DumpFtraceStats(this, &stats_after);
    auto after_packet = writer_->NewTracePacket();
    auto out = after_packet->set_ftrace_stats();
    out->set_phase(protos::pbzero::FtraceStats::Phase::END_OF_TRACE);
    stats_after.Write(out);
    for (auto error : parse_errors_) {
      out->add_ftrace_parse_errors(error);
    }
  }
}

}  // namespace perfetto
