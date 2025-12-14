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

#include "src/traced/probes/ftrace/frozen_ftrace_data_source.h"

#include <memory>

#include "perfetto/base/task_runner.h"
#include "perfetto/ext/base/string_utils.h"
#include "perfetto/ext/tracing/core/trace_writer.h"
#include "perfetto/tracing/core/data_source_config.h"
#include "src/traced/probes/ftrace/compact_sched.h"
#include "src/traced/probes/ftrace/cpu_stats_parser.h"
#include "src/traced/probes/ftrace/event_info.h"
#include "src/traced/probes/ftrace/ftrace_config_muxer.h"
#include "src/traced/probes/ftrace/ftrace_stats.h"
#include "src/traced/probes/ftrace/proto_translation_table.h"
#include "src/traced/probes/ftrace/tracefs.h"

#include "protos/perfetto/trace/ftrace/ftrace_stats.pbzero.h"
#include "protos/perfetto/trace/trace_packet.pbzero.h"

namespace perfetto {

// static
const ProbesDataSource::Descriptor FrozenFtraceDataSource::descriptor = {
    /*name*/ "linux.frozen_ftrace",
    /*flags*/ Descriptor::kFlagsNone,
    /*fill_descriptor_func*/ nullptr,
};

FrozenFtraceDataSource::~FrozenFtraceDataSource() {
  // Ensure the read data is erased and not recovered in the next boot.
  if (tracefs_)
    tracefs_->ClearTrace();
}

FrozenFtraceDataSource::FrozenFtraceDataSource(
    base::TaskRunner* task_runner,
    const DataSourceConfig& ds_config,
    TracingSessionID session_id,
    std::unique_ptr<TraceWriter> writer)
    : ProbesDataSource(session_id, &descriptor),
      task_runner_(task_runner),
      writer_(std::move(writer)),
      weak_factory_(this) {
  // This should check the required parameters.
  ds_config_.ParseFromString(ds_config.frozen_ftrace_config_raw());
}

void FrozenFtraceDataSource::Start() {
  parsing_mem_.AllocateIfNeeded();

  std::string raw_instance_name = ds_config_.instance_name();
  if (base::Contains(raw_instance_name, '/') ||
      base::StartsWith(raw_instance_name, "..")) {
    PERFETTO_ELOG("instance name '%s' is invalid.", raw_instance_name.c_str());
    return;
  }

  tracefs_ =
      Tracefs::CreateGuessingMountPoint("instances/" + raw_instance_name + "/");
  if (!tracefs_)
    return;

  translation_table_ = ProtoTranslationTable::Create(
      tracefs_.get(), GetStaticEventInfo(), GetStaticCommonFieldsInfo());
  if (!translation_table_) {
    PERFETTO_ELOG("Failed to create translation table.");
    return;
  }

  // Assumes the same core count as currently. If not, the previous boot
  // data is cleared because of the failure of buffer metadata validation.
  size_t num_cpus = tracefs_->NumberOfCpus();

  // To avoid reading pages more than expected, record remaining pages.
  size_t initial_page_quota = tracefs_->GetCpuBufferSizeInPages();

  PERFETTO_CHECK(cpu_readers_.empty());
  cpu_readers_.reserve(num_cpus);
  for (size_t cpu = 0; cpu < num_cpus; cpu++) {
    cpu_readers_.emplace_back(cpu, tracefs_->OpenPipeForCpu(cpu),
                              translation_table_.get(),
                              /*symbolizer=*/nullptr);

    cpu_page_quota_.push_back(initial_page_quota);
  }
  if (cpu_readers_.empty())
    return;

  // Enable all events in the translation table because the previous
  // boot trace data may record any events.
  EventFilter event_filter;
  for (const auto& event : translation_table_->events()) {
    event_filter.AddEnabledEvent(event.ftrace_event_id);
  }

  parsing_config_ =
      std::unique_ptr<FtraceDataSourceConfig>(new FtraceDataSourceConfig(
          /*event_filter=*/std::move(event_filter),
          /*syscall_filter=*/EventFilter{},
          /*compact_sched_in=*/CompactSchedConfig{false},
          /*print_filter=*/std::nullopt,
          /*atrace_apps=*/{},
          /*atrace_categories=*/{},
          /*atrace_categories_sdk_optout=*/{},
          /*symbolize_ksyms=*/false,
          /*buffer_percent=*/0u,
          /*syscalls_returning_fd=*/{},
          /*kprobes=*/
          base::FlatHashMap<uint32_t, protos::pbzero::KprobeEvent::KprobeType>{
              0},
          /*debug_ftrace_abi=*/false,
          /*write_generic_evt_descriptors=*/false));

  // For serialising pre-existing ftrace data, emit a special packet so that
  // trace_processor doesn't filter out data before start-of-trace.
  auto stats_packet = writer_->NewTracePacket();
  auto* stats = stats_packet->set_ftrace_stats();
  stats->set_phase(protos::pbzero::FtraceStats::Phase::START_OF_TRACE);
  stats->set_preserve_ftrace_buffer(true);

  // Start the reader tasks, which will self-repost until the existing raw
  // buffer pages have been parsed. The work is split into tasks to let other
  // ipc/tasks to run inbetween.
  task_runner_->PostTask([weak_this = weak_factory_.GetWeakPtr()] {
    if (weak_this)
      weak_this->ReadTask();
  });
}

void FrozenFtraceDataSource::ReadTask() {
  bool all_cpus_done = true;
  for (size_t i = 0; i < cpu_readers_.size(); i++) {
    size_t max_pages =
        std::min<size_t>(kFrozenFtraceMaxReadPages, cpu_page_quota_[i]);
    if (max_pages == 0)
      continue;

    size_t pages_read = cpu_readers_[i].ReadFrozen(
        &parsing_mem_, max_pages, parsing_config_.get(), &metadata_,
        &parse_errors_, writer_.get());
    PERFETTO_DCHECK(pages_read <= max_pages);

    if (pages_read != 0) {
      all_cpus_done = false;
    }
    cpu_page_quota_[i] -= pages_read;
  }

  // More work to do, repost the task at the end of the queue.
  if (!all_cpus_done) {
    task_runner_->PostTask([weak_this = weak_factory_.GetWeakPtr()] {
      if (weak_this)
        weak_this->ReadTask();
    });
    return;
  }

  // Finished. Write the end of trace packet.
  {
    FtraceStats stats_after{};
    DumpAllCpuStats(tracefs_.get(), &stats_after);
    auto after_packet = writer_->NewTracePacket();
    auto out = after_packet->set_ftrace_stats();
    out->set_phase(protos::pbzero::FtraceStats::Phase::END_OF_TRACE);
    stats_after.Write(out);
    for (auto error : parse_errors_) {
      out->add_ftrace_parse_errors(error);
    }
  }
}

void FrozenFtraceDataSource::Flush(FlushRequestID,
                                   std::function<void()> callback) {
  writer_->Flush(std::move(callback));
}

}  // namespace perfetto
