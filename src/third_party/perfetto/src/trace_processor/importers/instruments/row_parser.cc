/*
 * Copyright (C) 2024 The Android Open Source Project
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

#include "src/trace_processor/importers/instruments/row_parser.h"

#include <cstdint>
#include <optional>

#include "perfetto/base/build_config.h"
#include "perfetto/ext/base/flat_hash_map.h"
#include "perfetto/ext/base/string_view.h"
#include "src/trace_processor/importers/common/address_range.h"
#include "src/trace_processor/importers/common/cpu_tracker.h"
#include "src/trace_processor/importers/common/import_logs_tracker.h"
#include "src/trace_processor/importers/common/mapping_tracker.h"
#include "src/trace_processor/importers/common/process_tracker.h"
#include "src/trace_processor/importers/common/profiler_sample_tracker.h"
#include "src/trace_processor/importers/common/stack_profile_tracker.h"
#include "src/trace_processor/importers/common/virtual_memory_mapping.h"
#include "src/trace_processor/importers/instruments/row.h"
#include "src/trace_processor/importers/instruments/row_data_tracker.h"
#include "src/trace_processor/storage/stats.h"
#include "src/trace_processor/storage/trace_storage.h"
#include "src/trace_processor/util/build_id.h"

#if !PERFETTO_BUILDFLAG(PERFETTO_TP_INSTRUMENTS)
#error \
    "This file should not be built when enable_perfetto_trace_processor_mac_instruments=false"
#endif

namespace perfetto::trace_processor::instruments_importer {

RowParser::RowParser(TraceProcessorContext* context, RowDataTracker& data)
    : context_(context),
      data_(data),
      instruments_source_id_(context->storage->InternString("instruments")) {}

RowParser::~RowParser() = default;

void RowParser::Parse(int64_t ts, instruments_importer::Row row) {
  if (!row.backtrace) {
    return;
  }

  Thread* thread = data_.GetThread(row.thread);
  if (!thread) {
    context_->import_logs_tracker->RecordParserLog(
        stats::instruments_row_missing_thread, ts);
    return;
  }
  Process* process = data_.GetProcess(thread->process);
  if (!process) {
    context_->import_logs_tracker->RecordParserLog(
        stats::instruments_row_missing_process, ts);
    return;
  }
  uint32_t tid = static_cast<uint32_t>(thread->tid);
  uint32_t pid = static_cast<uint32_t>(process->pid);

  UniqueTid utid = context_->process_tracker->UpdateThread(tid, pid);
  UniquePid upid = context_->process_tracker->GetOrCreateProcess(pid);

  // TODO(leszeks): Avoid setting thread/process name if we've already seen this
  // Thread* / Process*.
  context_->process_tracker->UpdateThreadName(utid, thread->fmt,
                                              ThreadNamePriority::kOther);
  context_->process_tracker->UpdateProcessName(upid, process->fmt,
                                               ProcessNamePriority::kOther);

  auto& stack_profile_tracker = *context_->stack_profile_tracker;

  Backtrace* backtrace = data_.GetBacktrace(row.backtrace);
  if (!backtrace) {
    context_->import_logs_tracker->RecordParserLog(
        stats::instruments_row_missing_backtrace, ts);
    return;
  }
  std::optional<CallsiteId> parent;
  uint32_t depth = 0;
  auto leaf = backtrace->frames.rend() - 1;
  for (auto it = backtrace->frames.rbegin(); it != backtrace->frames.rend();
       ++it) {
    Frame* frame = data_.GetFrame(*it);
    if (!frame) {
      context_->import_logs_tracker->RecordParserLog(
          stats::instruments_row_missing_frame, ts);
      continue;
    }
    Binary* binary = data_.GetBinary(frame->binary);

    uint64_t pc = static_cast<uint64_t>(frame->addr);
    // GetBinary returns null for a null or out-of-range binary id, so gate the
    // load_addr adjustment on the resolved pointer rather than the raw id.
    if (binary) {
      pc -= static_cast<uint64_t>(binary->load_addr);
    }

    // For non-leaf functions, the pc will be after the end of the call. Adjust
    // it to be within the call instruction.
    if (pc != 0 && it != leaf) {
      --pc;
    }

    VirtualMemoryMapping* mapping = nullptr;
    mapping = context_->mapping_tracker->FindUserMappingForAddress(upid, pc);
    if (!mapping) {
      if (binary == nullptr) {
        mapping = GetDummyMapping(upid);
      } else {
        auto mapping_inserted =
            binary_to_mapping_.Insert(frame->binary, nullptr);
        if (mapping_inserted.second) {
          BuildId build_id = binary->uuid;
          *mapping_inserted.first =
              &context_->mapping_tracker->CreateUserMemoryMapping(
                  upid, {AddressRange(static_cast<uint64_t>(binary->load_addr),
                                      static_cast<uint64_t>(binary->max_addr)),
                         static_cast<uint64_t>(binary->load_addr), 0, 0,
                         binary->path, build_id});
        }
        mapping = *mapping_inserted.first;
      }
    }

    FrameId frame_id = mapping->InternFrame(mapping->ToRelativePc(pc),
                                            base::StringView(frame->name));

    parent = stack_profile_tracker.InternCallsite(parent, frame_id, depth);
    depth++;
  }

  tables::ProfilerSampleTable::Row sample_row;
  sample_row.ts = ts;
  sample_row.source = instruments_source_id_;
  tables::ProfilerTaskContextTable::Row task_context;
  task_context.utid = utid;
  task_context.upid = upid;
  sample_row.task_context_id =
      context_->profiler_sample_tracker->InternTaskContext(task_context);
  tables::ProfilerExecutionContextTable::Row execution_context;
  execution_context.ucpu =
      context_->cpu_tracker->GetOrCreateCpu(row.core_id).value;
  sample_row.execution_context_id =
      context_->profiler_sample_tracker->InternExecutionContext(
          execution_context);
  sample_row.callsite_id = parent;
  context_->profiler_sample_tracker->AddSample(sample_row);
}

DummyMemoryMapping* RowParser::GetDummyMapping(UniquePid upid) {
  if (auto it = dummy_mappings_.Find(upid); it) {
    return *it;
  }

  DummyMemoryMapping* mapping =
      &context_->mapping_tracker->CreateDummyMapping("");
  dummy_mappings_.Insert(upid, mapping);
  return mapping;
}

}  // namespace perfetto::trace_processor::instruments_importer
