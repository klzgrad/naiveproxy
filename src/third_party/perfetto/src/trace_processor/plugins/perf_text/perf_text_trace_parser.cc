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

#include "src/trace_processor/plugins/perf_text/perf_text_trace_parser.h"

#include <cstdint>

#include "src/trace_processor/importers/common/process_tracker.h"
#include "src/trace_processor/importers/common/profiler_sample_tracker.h"
#include "src/trace_processor/importers/common/stack_profile_tracker.h"
#include "src/trace_processor/plugins/perf_text/perf_text_event.h"
#include "src/trace_processor/storage/trace_storage.h"
#include "src/trace_processor/tables/profiler_tables_py.h"
#include "src/trace_processor/types/trace_processor_context.h"

namespace perfetto::trace_processor::perf_text_importer {

PerfTextTraceParser::PerfTextTraceParser(TraceProcessorContext* context)
    : context_(context),
      perf_text_source_id_(context->storage->InternString("perf_text")) {}

PerfTextTraceParser::~PerfTextTraceParser() = default;

void PerfTextTraceParser::Parse(int64_t ts, PerfTextEvent evt) {
  tables::ProfilerSampleTable::Row row;
  row.ts = ts;
  row.source = perf_text_source_id_;
  row.callsite_id = evt.callsite_id;
  UniqueTid utid =
      evt.pid ? context_->process_tracker->UpdateThread(evt.tid, *evt.pid)
              : context_->process_tracker->GetOrCreateThread(evt.tid);
  tables::ProfilerTaskContextTable::Row task_context;
  task_context.utid = utid;
  if (evt.pid) {
    task_context.upid = context_->process_tracker->GetOrCreateProcess(*evt.pid);
  }
  row.task_context_id =
      context_->profiler_sample_tracker->InternTaskContext(task_context);
  if (evt.comm) {
    context_->process_tracker->UpdateThreadNameAndMaybeProcessName(
        utid, *evt.comm, ThreadNamePriority::kOther);
  }
  context_->profiler_sample_tracker->AddSample(row);
}

}  // namespace perfetto::trace_processor::perf_text_importer
