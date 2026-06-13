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

#include "src/trace_processor/importers/simpleperf_proto/simpleperf_proto_parser.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

#include "perfetto/ext/base/string_view.h"
#include "src/trace_processor/importers/common/mapping_tracker.h"
#include "src/trace_processor/importers/common/process_tracker.h"
#include "src/trace_processor/importers/common/stack_profile_tracker.h"
#include "src/trace_processor/importers/common/virtual_memory_mapping.h"
#include "src/trace_processor/importers/simpleperf_proto/simpleperf_proto_tracker.h"
#include "src/trace_processor/storage/stats.h"
#include "src/trace_processor/storage/trace_storage.h"

#include "protos/third_party/simpleperf/cmd_report_sample.pbzero.h"
#include "src/trace_processor/tables/profiler_tables_py.h"

namespace perfetto::trace_processor::simpleperf_proto_importer {

SimpleperfProtoParser::SimpleperfProtoParser(TraceProcessorContext* context,
                                             SimpleperfProtoTracker* tracker)
    : context_(context), tracker_(tracker) {}

SimpleperfProtoParser::~SimpleperfProtoParser() = default;

void SimpleperfProtoParser::Parse(int64_t ts,
                                  const SimpleperfProtoEvent& event) {
  using namespace perfetto::third_party::simpleperf::proto::pbzero;
  Record::Decoder record(event.record_data.data(), event.record_data.size());

  if (record.has_sample()) {
    Sample::Decoder sample(record.sample());

    auto tid = static_cast<uint32_t>(sample.thread_id());
    UniqueTid utid = context_->process_tracker->GetOrCreateThread(tid);

    // Simpleperf provides callchain in leaf-to-root order, but Perfetto uses
    // depth 0 = root convention. Reverse the entries to build root-to-leaf.
    struct CallChainData {
      uint64_t vaddr;
      uint32_t file_id;
      int32_t symbol_id;
    };
    std::vector<CallChainData> entries;
    for (auto it = sample.callchain(); it; ++it) {
      using CallChainEntry = perfetto::third_party::simpleperf::proto::pbzero::
          Sample::CallChainEntry;
      CallChainEntry::Decoder entry(*it);
      entries.push_back(
          {entry.vaddr_in_file(), entry.file_id(), entry.symbol_id()});
    }

    // Build callchain from root to leaf (depth 0 = root)
    std::optional<CallsiteId> callsite_id;
    uint32_t depth = 0;

    for (auto rit = entries.rbegin(); rit != entries.rend(); ++rit) {
      uint64_t vaddr = rit->vaddr;
      uint32_t file_id = rit->file_id;
      int32_t symbol_id = rit->symbol_id;

      // Resolve symbol name from symbol table
      std::optional<StringId> symbol_name_id =
          tracker_->GetSymbol(file_id, symbol_id);

      // Get mapping for this file
      DummyMemoryMapping* mapping = tracker_->GetMapping(file_id);
      if (!mapping) {
        // Drop sample if file_id not found
        context_->storage->IncrementStats(
            stats::simpleperf_missing_file_mapping);
        return;
      }

      // Intern frame with virtual address and symbol name
      // Use StringId::Null() for missing symbols to get SQL NULL
      StringId name_id = symbol_name_id.value_or(StringId::Null());
      base::StringView symbol_view = context_->storage->GetString(name_id);
      FrameId frame_id = mapping->InternFrame(vaddr, symbol_view);

      // Intern callsite (building from root to leaf, depth 0 = root)
      callsite_id = context_->stack_profile_tracker->InternCallsite(
          callsite_id, frame_id, depth);
      depth++;
    }

    // Insert into cpu_profile_stack_sample table with the leaf callsite
    // (the last callsite created, which has the highest depth)
    if (callsite_id.has_value()) {
      tables::CpuProfileStackSampleTable::Row row;
      row.ts = ts;
      row.callsite_id = *callsite_id;
      row.utid = utid;
      row.process_priority = 0;  // Default priority
      context_->storage->mutable_cpu_profile_stack_sample_table()->Insert(row);
    }
    return;
  }

  if (record.has_thread()) {
    Thread::Decoder thread(record.thread());
    uint32_t tid = thread.thread_id();
    uint32_t pid = thread.process_id();

    if (tid != 0 && pid != 0) {
      UniqueTid utid = context_->process_tracker->UpdateThread(tid, pid);

      if (thread.has_thread_name()) {
        base::StringView thread_name_view(thread.thread_name().data,
                                          thread.thread_name().size);
        StringId name_id = context_->storage->InternString(thread_name_view);
        context_->process_tracker->UpdateThreadName(utid, name_id,
                                                    ThreadNamePriority::kOther);
      }
    }
    return;
  }

  if (record.has_context_switch()) {
    // TODO(lalitm): Parse ContextSwitch record and insert into sched_slice
    // table. This contains:
    // - switch_on: bool indicating if thread is scheduled on (true) or off
    // (false)
    // - time: timestamp in nanoseconds (monotonic clock, or perf clock on
    // kernel < 4.1)
    // - thread_id: TID of the thread being switched
    // Implementation should:
    // 1. Track switch_on/switch_off pairs to create sched_slice entries
    // 2. Match thread_id to utid using process_tracker
    // 3. Determine CPU from previous state or metadata
    // 4. Insert into sched_slice table with proper ts, dur, cpu, and utid
    return;
  }
}

}  // namespace perfetto::trace_processor::simpleperf_proto_importer
