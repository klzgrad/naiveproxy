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
#include "src/trace_processor/importers/common/mapping_tracker.h"
#include "src/trace_processor/importers/common/process_tracker.h"
#include "src/trace_processor/importers/common/stack_profile_tracker.h"
#include "src/trace_processor/importers/common/virtual_memory_mapping.h"
#include "src/trace_processor/importers/instruments/row.h"
#include "src/trace_processor/importers/instruments/row_data_tracker.h"
#include "src/trace_processor/storage/trace_storage.h"
#include "src/trace_processor/util/build_id.h"

#if !PERFETTO_BUILDFLAG(PERFETTO_TP_INSTRUMENTS)
#error \
    "This file should not be built when enable_perfetto_trace_processor_mac_instruments=false"
#endif

namespace perfetto::trace_processor::instruments_importer {

RowParser::RowParser(TraceProcessorContext* context, RowDataTracker& data)
    : context_(context), data_(data) {}

RowParser::~RowParser() = default;

void RowParser::Parse(int64_t ts, instruments_importer::Row row) {
  if (!row.backtrace) {
    return;
  }

  Thread* thread = data_.GetThread(row.thread);
  Process* process = data_.GetProcess(thread->process);
  uint32_t tid = static_cast<uint32_t>(thread->tid);
  uint32_t pid = static_cast<uint32_t>(process->pid);

  UniqueTid utid = context_->process_tracker->UpdateThread(tid, pid);
  UniquePid upid = context_->process_tracker->GetOrCreateProcess(pid);

  // TODO(leszeks): Avoid setting thread/process name if we've already seen this
  // Thread* / Process*.
  context_->process_tracker->UpdateThreadName(utid, thread->fmt,
                                              ThreadNamePriority::kOther);
  context_->process_tracker->SetProcessNameIfUnset(upid, process->fmt);

  auto& stack_profile_tracker = *context_->stack_profile_tracker;

  Backtrace* backtrace = data_.GetBacktrace(row.backtrace);
  std::optional<CallsiteId> parent;
  uint32_t depth = 0;
  auto leaf = backtrace->frames.rend() - 1;
  for (auto it = backtrace->frames.rbegin(); it != backtrace->frames.rend();
       ++it) {
    Frame* frame = data_.GetFrame(*it);
    Binary* binary = data_.GetBinary(frame->binary);

    uint64_t pc = static_cast<uint64_t>(frame->addr);
    if (frame->binary) {
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

  context_->storage->mutable_instruments_sample_table()->Insert(
      {ts, utid, parent, row.core_id});
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
