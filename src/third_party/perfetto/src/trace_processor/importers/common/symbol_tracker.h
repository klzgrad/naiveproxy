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

#ifndef SRC_TRACE_PROCESSOR_IMPORTERS_COMMON_SYMBOL_TRACKER_H_
#define SRC_TRACE_PROCESSOR_IMPORTERS_COMMON_SYMBOL_TRACKER_H_

#include <cstdint>
#include <string>

#include "perfetto/ext/base/flat_hash_map.h"
#include "src/trace_processor/importers/common/address_range.h"
#include "src/trace_processor/storage/trace_storage.h"
#include "src/trace_processor/tables/profiler_tables_py.h"
#include "src/trace_processor/types/trace_processor_context.h"

namespace perfetto::trace_processor {

class SymbolTracker {
 public:
  // DSO = Dynamic Shared Object.
  struct Dso {
    uint64_t load_bias = 0;
    AddressRangeMap<std::string> symbols;
    bool symbols_are_absolute = false;
  };

  explicit SymbolTracker(TraceProcessorContext* context);
  ~SymbolTracker();

  void OnEventsFullyExtracted();

  AddressRangeMap<std::string>& kernel_symbols() { return kernel_symbols_; }
  base::FlatHashMap<StringId, Dso>& dsos() { return dsos_; }

 private:
  void SymbolizeKernelFrame(tables::StackProfileFrameTable::RowReference frame);

  // Returns true it the frame was symbolized.
  bool TrySymbolizeFrame(tables::StackProfileFrameTable::RowReference frame);

  TraceProcessorContext* const context_;

  const tables::StackProfileMappingTable& mapping_table_;
  AddressRangeMap<std::string> kernel_symbols_;
  base::FlatHashMap<StringId, Dso> dsos_;
};

}  // namespace perfetto::trace_processor

#endif  // SRC_TRACE_PROCESSOR_IMPORTERS_COMMON_SYMBOL_TRACKER_H_
