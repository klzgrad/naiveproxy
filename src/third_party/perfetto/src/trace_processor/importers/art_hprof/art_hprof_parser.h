/*
 * Copyright (C) 2025 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#ifndef SRC_TRACE_PROCESSOR_IMPORTERS_ART_HPROF_ART_HPROF_PARSER_H_
#define SRC_TRACE_PROCESSOR_IMPORTERS_ART_HPROF_ART_HPROF_PARSER_H_

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "perfetto/base/status.h"
#include "perfetto/ext/base/flat_hash_map.h"
#include "perfetto/trace_processor/trace_blob_view.h"
#include "src/trace_processor/importers/art_hprof/art_heap_graph.h"
#include "src/trace_processor/importers/art_hprof/art_heap_graph_builder.h"
#include "src/trace_processor/importers/common/chunked_trace_reader.h"
#include "src/trace_processor/storage/trace_storage.h"
#include "src/trace_processor/tables/profiler_tables_py.h"
#include "src/trace_processor/types/trace_processor_context.h"
#include "src/trace_processor/util/trace_blob_view_reader.h"

namespace perfetto::trace_processor::art_hprof {

constexpr const char* kJavaLangObject = "java.lang.Object";
constexpr const char* kUnknownClassKind = "[unknown class kind]";
constexpr size_t kRecordLengthOffset = 5;

class ArtHprofParser : public ChunkedTraceReader {
 public:
  explicit ArtHprofParser(TraceProcessorContext* context);
  ~ArtHprofParser() override;
  base::Status Parse(TraceBlobView blob) override;
  base::Status NotifyEndOfFile() override;

 private:
  void PopulateClasses(const HeapGraph& graph);
  void PopulateObjects(const HeapGraph& graph, int64_t ts, UniquePid upid);
  void PopulateReferences(const HeapGraph& graph);

  // Helper methods
  tables::HeapGraphClassTable::Id* FindClassId(uint64_t class_id) const;
  tables::HeapGraphObjectTable::Id* FindObjectId(uint64_t obj_id) const;
  tables::HeapGraphClassTable::Id* FindClassObjectId(uint64_t obj_id) const;
  StringId InternClassName(const std::string& class_name);

  class TraceBlobViewIterator : public ByteIterator {
   public:
    explicit TraceBlobViewIterator();
    ~TraceBlobViewIterator() override;
    bool ReadU1(uint8_t& value) override;
    bool ReadU2(uint16_t& value) override;
    bool ReadU4(uint32_t& value) override;
    bool ReadId(uint64_t& value, uint32_t id_size) override;
    bool ReadString(std::string& str, size_t length) override;
    bool ReadBytes(std::vector<uint8_t>& data, size_t length) override;
    bool SkipBytes(size_t count) override;
    size_t GetPosition() const override;
    // Whether we can read an entire record from the existing chunk.
    // This method does not advance the iterator.
    bool CanReadRecord() const override;
    // Pushes more HPROF chunks in for parsin.
    void PushBlob(TraceBlobView blob) override;
    // Shrinks the backing HPROF data to discard all consumed bytes.
    void Shrink() override;

   private:
    util::TraceBlobViewReader reader_;
    size_t current_offset_ = 0;
  };

  TraceProcessorContext* const context_;

  // Parser components
  std::unique_ptr<ByteIterator> byte_iterator_;
  std::unique_ptr<HeapGraphBuilder> parser_;

  // Maps moved to instance variables
  base::FlatHashMap<uint64_t, tables::HeapGraphClassTable::Id> class_map_;
  base::FlatHashMap<uint64_t, tables::HeapGraphClassTable::Id>
      class_object_map_;
  base::FlatHashMap<uint64_t, tables::HeapGraphObjectTable::Id> object_map_;
  // For class objects that are denoted with "java.lang.Class<"
  base::FlatHashMap<uint64_t, std::string> class_name_map_;
};
}  // namespace perfetto::trace_processor::art_hprof
#endif  // SRC_TRACE_PROCESSOR_IMPORTERS_ART_HPROF_ART_HPROF_PARSER_H_
