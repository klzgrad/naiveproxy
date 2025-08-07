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

#ifndef SRC_TRACE_PROCESSOR_IMPORTERS_ART_HPROF_ART_HEAP_GRAPH_BUILDER_H_
#define SRC_TRACE_PROCESSOR_IMPORTERS_ART_HPROF_ART_HEAP_GRAPH_BUILDER_H_

#include "perfetto/ext/base/flat_hash_map.h"
#include "perfetto/trace_processor/trace_blob_view.h"
#include "src/trace_processor/importers/art_hprof/art_heap_graph.h"
#include "src/trace_processor/importers/art_hprof/art_hprof_model.h"
#include "src/trace_processor/importers/art_hprof/art_hprof_types.h"
#include "src/trace_processor/storage/stats.h"
#include "src/trace_processor/storage/trace_storage.h"
#include "src/trace_processor/types/trace_processor_context.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace perfetto::trace_processor::art_hprof {

constexpr uint32_t kHprofHeaderMagic = 0x4A415641;  // "JAVA" in ASCII
constexpr size_t kHprofHeaderLength = 20;           // Header size in bytes

constexpr const char* kJavaLangString = "java.lang.String";
constexpr const char* kSunMiscCleaner = "sun.misc.Cleaner";

class ByteIterator {
 public:
  virtual ~ByteIterator();

  virtual bool ReadU1(uint8_t& value) = 0;
  virtual bool ReadU2(uint16_t& value) = 0;
  virtual bool ReadU4(uint32_t& value) = 0;
  virtual bool ReadId(uint64_t& value, uint32_t id_size) = 0;
  virtual bool ReadString(std::string& str, size_t length) = 0;
  virtual bool ReadBytes(std::vector<uint8_t>& data, size_t length) = 0;
  virtual bool SkipBytes(size_t count) = 0;
  virtual void PushBlob(TraceBlobView data) = 0;

  virtual size_t GetPosition() const = 0;
  virtual bool CanReadRecord() const = 0;
  virtual void Shrink() = 0;
};

// Statistics collected during heap graph building
struct DebugStats {
  size_t string_count = 0;
  size_t class_count = 0;
  size_t heap_dump_count = 0;
  size_t instance_count = 0;
  size_t object_array_count = 0;
  size_t primitive_array_count = 0;
  size_t root_count = 0;
  size_t reference_count = 0;
  size_t record_count = 0;

  void Write(TraceProcessorContext* context_) const {
    context_->storage->SetStats(stats::hprof_string_counter,
                                static_cast<int64_t>(string_count));
    context_->storage->SetStats(stats::hprof_class_counter,
                                static_cast<int64_t>(class_count));
    context_->storage->SetStats(stats::hprof_heap_dump_counter,
                                static_cast<int64_t>(heap_dump_count));
    context_->storage->SetStats(stats::hprof_instance_counter,
                                static_cast<int64_t>(instance_count));
    context_->storage->SetStats(stats::hprof_object_array_counter,
                                static_cast<int64_t>(object_array_count));
    context_->storage->SetStats(stats::hprof_primitive_array_counter,
                                static_cast<int64_t>(primitive_array_count));
    context_->storage->SetStats(stats::hprof_reference_counter,
                                static_cast<int64_t>(reference_count));
    context_->storage->SetStats(stats::hprof_root_counter,
                                static_cast<int64_t>(root_count));
  }

  void AddRecordCount(size_t count) { record_count += count; }
};

// Resolves references, extracts field values, and builds the complete object
// graph
class HeapGraphResolver {
 public:
  HeapGraphResolver(TraceProcessorContext* context,
                    HprofHeader& header,
                    base::FlatHashMap<uint64_t, Object>& objects,
                    base::FlatHashMap<uint64_t, ClassDefinition>& classes,
                    base::FlatHashMap<uint64_t, HprofHeapRootTag>& roots,
                    DebugStats& stats);

  // Build the complete object graph with references and field values
  void ResolveGraph();

 private:
  // Extract data for all objects
  void ExtractAllObjectData();

  // Mark objects reachable from roots
  void MarkReachableObjects();

  // Extract references from array elements
  void ExtractArrayElementReferences(Object& obj);

  // Helper methods for data extraction
  bool ExtractObjectReferences(Object& obj, const ClassDefinition& cls);
  void ExtractFieldValues(Object& obj, const ClassDefinition& cls);
  void ExtractPrimitiveArrayValues(Object& obj);
  std::optional<std::string> DecodeJavaString(const Object& string_obj) const;

  // Utility methods
  std::vector<Field> GetClassHierarchyFields(uint64_t class_id) const;

  // Calculate native memory sizes for objects
  void CalculateNativeSizes();

  // Data references (not owned)
  TraceProcessorContext* context_;
  HprofHeader& header_;
  base::FlatHashMap<uint64_t, Object>& objects_;
  base::FlatHashMap<uint64_t, HprofHeapRootTag>& roots_;
  base::FlatHashMap<uint64_t, ClassDefinition>& classes_;
  DebugStats& stats_;
};

// Main parser class that builds a heap graph from HPROF data
class HeapGraphBuilder {
 public:
  explicit HeapGraphBuilder(std::unique_ptr<ByteIterator> iterator,
                            TraceProcessorContext* context);
  ~HeapGraphBuilder();

  // Disallow copy and move
  HeapGraphBuilder(const HeapGraphBuilder&) = delete;
  HeapGraphBuilder& operator=(const HeapGraphBuilder&) = delete;

  // Parse the HPROF file
  bool Parse();

  // Parse the HPROF file header
  bool ParseHeader();

  void PushBlob(TraceBlobView&& data);

  // Build and return the final heap graph
  HeapGraph BuildGraph();

 private:
  //--------------------------------------------------------------------------
  // Phase 1: File Header & Record Parsing
  //--------------------------------------------------------------------------
  // Parse a top-level HPROF record
  bool ParseRecord();

  // Parse a UTF-8 string table entry
  bool ParseUtf8StringRecord(uint32_t length);

  // Parse a class definition record
  bool ParseClassDefinition();

  //--------------------------------------------------------------------------
  // Phase 2: Heap Object Parsing
  //--------------------------------------------------------------------------
  // Parse a heap dump segment
  bool ParseHeapDump(size_t length);

  // Parse an individual heap dump record
  bool ParseHeapDumpRecord();

  // Parse a root reference record
  bool ParseRootRecord(HprofHeapRootTag tag);

  // Parse a class structure dump record
  bool ParseClassStructure();

  // Parse an instance object dump record
  bool ParseInstanceObject();

  // Parse an object array dump record
  bool ParseObjectArrayObject();

  // Parse a primitive array dump record
  bool ParsePrimitiveArrayObject();

  // Parse a heap name info record
  bool ParseHeapName();

  //--------------------------------------------------------------------------
  // Utility Methods
  //--------------------------------------------------------------------------
  // Look up a string by ID
  std::string LookupString(uint64_t id) const;

  void StoreString(uint64_t id, const std::string& str);

  // Convert JVM class name to Java format
  std::string NormalizeClassName(const std::string& name) const;

  //--------------------------------------------------------------------------
  // Data Members
  //--------------------------------------------------------------------------
  // Input data iterator
  std::unique_ptr<ByteIterator> iterator_;

  // HPROF file header
  HprofHeader header_;

  // Current heap name
  std::string current_heap_;

  // Data collections
  base::FlatHashMap<uint64_t, StringId> strings_;
  base::FlatHashMap<uint64_t, ClassDefinition> classes_;
  base::FlatHashMap<uint64_t, Object> objects_;

  // Type mapping and root tracking
  std::array<uint64_t, 12> prim_array_class_ids_ = {};
  base::FlatHashMap<uint64_t, HprofHeapRootTag> roots_;

  // Debug statistics
  DebugStats stats_;

  // Resolver for building the object graph
  std::unique_ptr<HeapGraphResolver> resolver_;
  TraceProcessorContext* context_;
};

// Helper method
inline size_t GetFieldTypeSize(FieldType type, size_t id_size) {
  switch (type) {
    case FieldType::kObject:
      return id_size;
    case FieldType::kBoolean:
    case FieldType::kByte:
      return 1;
    case FieldType::kChar:
    case FieldType::kShort:
      return 2;
    case FieldType::kFloat:
    case FieldType::kInt:
      return 4;
    case FieldType::kDouble:
    case FieldType::kLong:
      return 8;
  }

  return 0;
}
}  // namespace perfetto::trace_processor::art_hprof

#endif  // SRC_TRACE_PROCESSOR_IMPORTERS_ART_HPROF_ART_HEAP_GRAPH_BUILDER_H_
