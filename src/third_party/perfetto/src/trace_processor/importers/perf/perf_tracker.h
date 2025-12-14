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

#ifndef SRC_TRACE_PROCESSOR_IMPORTERS_PERF_PERF_TRACKER_H_
#define SRC_TRACE_PROCESSOR_IMPORTERS_PERF_PERF_TRACKER_H_

#include <cstdint>
#include <functional>
#include <memory>
#include <optional>

#include "perfetto/base/status.h"
#include "perfetto/ext/base/flat_hash_map.h"
#include "perfetto/ext/base/status_or.h"
#include "src/trace_processor/importers/common/create_mapping_params.h"
#include "src/trace_processor/importers/common/virtual_memory_mapping.h"
#include "src/trace_processor/importers/perf/auxtrace_info_record.h"
#include "src/trace_processor/storage/trace_storage.h"
#include "src/trace_processor/types/destructible.h"
#include "src/trace_processor/types/trace_processor_context.h"

#include "protos/third_party/simpleperf/record_file.pbzero.h"

namespace perfetto::trace_processor::etm {
class EtmTracker;
}

namespace perfetto::trace_processor::perf_importer {

class AuxDataTokenizer;

class PerfTracker {
 public:
  explicit PerfTracker(TraceProcessorContext* context);

  using AuxDataTokenizerFactory =
      std::function<base::StatusOr<std::unique_ptr<AuxDataTokenizer>>(
          TraceProcessorContext*,
          etm::EtmTracker*,
          AuxtraceInfoRecord)>;
  void RegisterAuxTokenizer(uint32_t type, AuxDataTokenizerFactory factory);

  base::StatusOr<std::unique_ptr<AuxDataTokenizer>> CreateAuxDataTokenizer(
      AuxtraceInfoRecord info);

  // Add symbol data contained in a `FileFeature` proto.
  void AddSimpleperfFile2(
      const third_party::simpleperf::proto::pbzero::FileFeature::Decoder& file);

  void CreateKernelMemoryMapping(int64_t trace_ts, CreateMappingParams params);
  void CreateUserMemoryMapping(int64_t trace_ts,
                               UniquePid upid,
                               CreateMappingParams params);

  base::Status NotifyEndOfFile();

 private:
  void AddMapping(int64_t trace_ts,
                  std::optional<UniquePid> upid,
                  const VirtualMemoryMapping& mapping);

  TraceProcessorContext* const context_;
  std::unique_ptr<Destructible> etm_tracker_;

  base::FlatHashMap<uint32_t, AuxDataTokenizerFactory> factories_;
};

}  // namespace perfetto::trace_processor::perf_importer

#endif  // SRC_TRACE_PROCESSOR_IMPORTERS_PERF_PERF_TRACKER_H_
