/*
 * Copyright (C) 2026 The Android Open Source Project
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

#ifndef SRC_TRACE_PROCESSOR_IMPORTERS_ART_METHOD_ART_METHOD_V2_TOKENIZER_H_
#define SRC_TRACE_PROCESSOR_IMPORTERS_ART_METHOD_ART_METHOD_V2_TOKENIZER_H_

#include <cstdint>
#include <memory>
#include <vector>

#include "perfetto/base/status.h"
#include "perfetto/ext/base/flat_hash_map.h"
#include "perfetto/ext/base/status_or.h"
#include "perfetto/trace_processor/trace_blob_view.h"
#include "src/trace_processor/importers/art_method/art_method_event.h"
#include "src/trace_processor/importers/common/chunked_trace_reader.h"
#include "src/trace_processor/sorter/trace_sorter.h"
#include "src/trace_processor/storage/trace_storage.h"
#include "src/trace_processor/types/trace_processor_context.h"
#include "src/trace_processor/util/trace_blob_view_reader.h"

namespace perfetto::trace_processor::art_method {

class ArtMethodV2Tokenizer : public ChunkedTraceReader {
 public:
  explicit ArtMethodV2Tokenizer(TraceProcessorContext*);
  ~ArtMethodV2Tokenizer() override;

  base::Status Parse(TraceBlobView blob) override;
  base::Status OnPushDataToSorter() override;
  void OnEventsFullyExtracted() override;

 private:
  struct MethodInfo {
    StringId name;
    std::optional<StringId> pathname;
    std::optional<uint32_t> line_number;
  };

  struct ThreadInfo {
    StringId comm;
    bool comm_used = false;
    std::vector<uint64_t> method_stack;
  };

  base::StatusOr<bool> ParseHeader();
  base::StatusOr<bool> ParseThreadOrMethodInfo(bool is_method);
  void ParseMethod(uint64_t id, const std::string& str);
  base::StatusOr<bool> ParseTraceEntries();
  void PushRecord(uint32_t tid,
                  uint32_t action,
                  uint64_t method_id,
                  int64_t ts);

  TraceProcessorContext* context_ = nullptr;
  std::unique_ptr<TraceSorter::Stream<ArtMethodEvent>> stream_;
  util::TraceBlobViewReader reader_;

  bool header_parsed_ = false;
  bool is_dual_clock_ = false;
  bool trace_complete_ = false;
  bool is_parsing_summary_ = false;
  int64_t ts_ = 0;
  uint64_t start_tsc_ = 0;
  uint64_t tsc_frequency_ = 0;
  std::string summary_;

  base::FlatHashMap<uint64_t, ThreadInfo> thread_map_;
  base::FlatHashMap<uint64_t, MethodInfo> method_map_;
};

}  // namespace perfetto::trace_processor::art_method

#endif  // SRC_TRACE_PROCESSOR_IMPORTERS_ART_METHOD_ART_METHOD_V2_TOKENIZER_H_
