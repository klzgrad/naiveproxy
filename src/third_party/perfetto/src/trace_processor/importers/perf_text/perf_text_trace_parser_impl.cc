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

#include "src/trace_processor/importers/perf_text/perf_text_trace_parser_impl.h"

#include <cstdint>

#include "src/trace_processor/importers/common/process_tracker.h"
#include "src/trace_processor/importers/common/stack_profile_tracker.h"
#include "src/trace_processor/importers/perf_text/perf_text_event.h"
#include "src/trace_processor/storage/trace_storage.h"
#include "src/trace_processor/tables/profiler_tables_py.h"
#include "src/trace_processor/types/trace_processor_context.h"

namespace perfetto::trace_processor::perf_text_importer {

PerfTextTraceParserImpl::PerfTextTraceParserImpl(TraceProcessorContext* context)
    : context_(context) {}

PerfTextTraceParserImpl::~PerfTextTraceParserImpl() = default;

void PerfTextTraceParserImpl::ParsePerfTextEvent(int64_t ts,
                                                 PerfTextEvent evt) {
  auto* ss = context_->storage->mutable_cpu_profile_stack_sample_table();
  tables::CpuProfileStackSampleTable::Row row;
  row.ts = ts;
  row.callsite_id = evt.callsite_id;
  row.utid = evt.pid
                 ? context_->process_tracker->UpdateThread(evt.tid, *evt.pid)
                 : context_->process_tracker->GetOrCreateThread(evt.tid);
  if (evt.comm) {
    context_->process_tracker->UpdateThreadNameAndMaybeProcessName(
        evt.tid, *evt.comm, ThreadNamePriority::kOther);
  }
  ss->Insert(row);
}

}  // namespace perfetto::trace_processor::perf_text_importer
