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

#include "src/trace_processor/importers/gecko/gecko_trace_parser_impl.h"

#include <cstdint>

#include "perfetto/base/compiler.h"
#include "src/trace_processor/importers/common/process_tracker.h"
#include "src/trace_processor/importers/common/stack_profile_tracker.h"
#include "src/trace_processor/importers/gecko/gecko_event.h"
#include "src/trace_processor/storage/trace_storage.h"
#include "src/trace_processor/tables/profiler_tables_py.h"
#include "src/trace_processor/types/trace_processor_context.h"

namespace perfetto::trace_processor::gecko_importer {

namespace {

template <typename T>
constexpr uint32_t GeckoOneOf() {
  return base::variant_index<GeckoEvent::OneOf, T>();
}

}  // namespace

GeckoTraceParserImpl::GeckoTraceParserImpl(TraceProcessorContext* context)
    : context_(context) {}

GeckoTraceParserImpl::~GeckoTraceParserImpl() = default;

void GeckoTraceParserImpl::ParseGeckoEvent(int64_t ts, GeckoEvent evt) {
  switch (evt.oneof.index()) {
    case GeckoOneOf<GeckoEvent::ThreadMetadata>(): {
      auto thread = std::get<GeckoEvent::ThreadMetadata>(evt.oneof);
      UniqueTid utid =
          context_->process_tracker->UpdateThread(thread.tid, thread.pid);
      context_->process_tracker->UpdateThreadNameByUtid(
          utid, thread.name, ThreadNamePriority::kOther);
      break;
    }
    case GeckoOneOf<GeckoEvent::StackSample>():
      auto sample = std::get<GeckoEvent::StackSample>(evt.oneof);
      auto* ss = context_->storage->mutable_cpu_profile_stack_sample_table();
      tables::CpuProfileStackSampleTable::Row row;
      row.ts = ts;
      row.callsite_id = sample.callsite_id;
      row.utid = context_->process_tracker->GetOrCreateThread(sample.tid);
      ss->Insert(row);
      break;
  }
}

}  // namespace perfetto::trace_processor::gecko_importer
