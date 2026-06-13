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

#include "src/trace_processor/importers/common/symbol_tracker.h"

#include <cstdint>

#include "perfetto/ext/base/string_view.h"
#include "src/trace_processor/storage/trace_storage.h"
#include "src/trace_processor/tables/profiler_tables_py.h"
#include "src/trace_processor/types/trace_processor_context.h"

namespace perfetto::trace_processor {

SymbolTracker::SymbolTracker(TraceProcessorContext* context)
    : context_(context),
      mapping_table_(context->storage->stack_profile_mapping_table()) {}

SymbolTracker::~SymbolTracker() = default;

void SymbolTracker::OnEventsFullyExtracted() {
  const StringId kEmptyString = context_->storage->InternString("");
  for (auto frame = context_->storage->mutable_stack_profile_frame_table()
                        ->IterateRows();
       frame; ++frame) {
    if (frame.name() != kNullStringId && frame.name() != kEmptyString) {
      continue;
    }
    if (!TrySymbolizeFrame(frame.ToRowReference())) {
      SymbolizeKernelFrame(frame.ToRowReference());
    }
  }
}

void SymbolTracker::SymbolizeKernelFrame(
    tables::StackProfileFrameTable::RowReference frame) {
  const auto mapping = *mapping_table_.FindById(frame.mapping());
  uint64_t address = static_cast<uint64_t>(frame.rel_pc()) +
                     static_cast<uint64_t>(mapping.start());
  auto symbol = kernel_symbols_.Find(address);
  if (symbol == kernel_symbols_.end()) {
    return;
  }
  frame.set_name(
      context_->storage->InternString(base::StringView(symbol->second)));
}

bool SymbolTracker::TrySymbolizeFrame(
    tables::StackProfileFrameTable::RowReference frame) {
  const auto mapping = *mapping_table_.FindById(frame.mapping());
  auto* file = dsos_.Find(mapping.name());
  if (!file) {
    return false;
  }

  int64_t pc = frame.rel_pc();

  // Load bias is something we can only determine by looking at the actual elf
  // file. Thus PERF_RECORD_MMAP{2} events do not record it. So we need to
  // potentially do an adjustment here if the load_bias tracked in the mapping
  // table and the one reported by the file are mismatched.
  pc += static_cast<int64_t>(file->load_bias) - mapping.load_bias();

  // If the symbols in the map are absolute, then we need to relativize against
  // the exact offset and then add to the start of the mapping.
  //
  // TODO(rsavitski): double check if this is confusing "exact_offset for the
  // purposes of llvm RO ELF header mappings" with "pgoff of the mapping"
  if (file->symbols_are_absolute) {
    pc -= mapping.exact_offset();
    pc += mapping.start();
  }

  auto symbol = file->symbols.Find(static_cast<uint64_t>(pc));
  if (symbol == file->symbols.end()) {
    return false;
  }
  frame.set_name(
      context_->storage->InternString(base::StringView(symbol->second)));
  return true;
}

}  // namespace perfetto::trace_processor
