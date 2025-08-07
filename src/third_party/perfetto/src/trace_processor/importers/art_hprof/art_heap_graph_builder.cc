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

#include "src/trace_processor/importers/art_hprof/art_heap_graph_builder.h"
#include <cinttypes>

namespace perfetto::trace_processor::art_hprof {

constexpr std::array<std::pair<const char*, FieldType>, 8> kPrimitiveArrayTypes{
    {
        {"boolean[]", FieldType::kBoolean},
        {"char[]", FieldType::kChar},
        {"float[]", FieldType::kFloat},
        {"double[]", FieldType::kDouble},
        {"byte[]", FieldType::kByte},
        {"short[]", FieldType::kShort},
        {"int[]", FieldType::kInt},
        {"long[]", FieldType::kLong},
    }};

ByteIterator::~ByteIterator() = default;

HeapGraphBuilder::HeapGraphBuilder(std::unique_ptr<ByteIterator> iterator,
                                   TraceProcessorContext* context)
    : iterator_(std::move(iterator)), context_(context) {}

HeapGraphBuilder::~HeapGraphBuilder() = default;

bool HeapGraphBuilder::Parse() {
  size_t record_count = 0;
  while (ParseRecord()) {
    record_count++;
  }

  stats_.AddRecordCount(record_count);

  return true;
}

void HeapGraphBuilder::PushBlob(TraceBlobView&& blob) {
  iterator_->PushBlob(std::move(blob));
}

HeapGraph HeapGraphBuilder::BuildGraph() {
  // Phase 3: Resolve the heap graph
  resolver_ = std::make_unique<HeapGraphResolver>(context_, header_, objects_,
                                                  classes_, roots_, stats_);
  resolver_->ResolveGraph();

  stats_.Write(context_);
  HeapGraph graph(header_.GetTimestamp());

  for (auto it = strings_.GetIterator(); it; ++it) {
    graph.AddString(it.key(), it.value());
  }

  for (auto it = classes_.GetIterator(); it; ++it) {
    graph.AddClass(it.value());
  }

  for (auto it = objects_.GetIterator(); it; ++it) {
    graph.AddObject(it.value());
  }

  return graph;
}

bool HeapGraphBuilder::ParseHeader() {
  // Read format string (null-terminated)
  std::string format;
  uint8_t byte;
  while (iterator_->ReadU1(byte) && byte != 0) {
    format.push_back(static_cast<char>(byte));
  }

  header_.SetFormat(format);

  uint32_t id_size;
  if (!iterator_->ReadU4(id_size)) {
    return false;
  }
  header_.SetIdSize(id_size);

  uint32_t high_time, low_time;
  if (!iterator_->ReadU4(high_time) || !iterator_->ReadU4(low_time)) {
    return false;
  }

  uint64_t timestamp = (static_cast<uint64_t>(high_time) << 32) | low_time;

  header_.SetTimestamp(timestamp);

  return true;
}

bool HeapGraphBuilder::ParseRecord() {
  // Shrinks the buffer to the current_offset we've parsed so far.
  // It doesn't matter if we do it at the start or end of the method.
  // We'll either shrink the n - 1 last records or n last records.
  iterator_->Shrink();
  if (!iterator_->CanReadRecord()) {
    return false;
  }

  uint8_t tag_value;
  if (!iterator_->ReadU1(tag_value)) {
    return false;
  }
  HprofTag tag = static_cast<HprofTag>(tag_value);

  uint32_t time;
  if (!iterator_->ReadU4(time)) {
    return false;
  }

  uint32_t length;
  if (!iterator_->ReadU4(length)) {
    return false;
  }

  // Handle record based on tag
  switch (tag) {
    case HprofTag::kUtf8:
      return ParseUtf8StringRecord(length);

    case HprofTag::kLoadClass:
      return ParseClassDefinition();

    case HprofTag::kHeapDump:
    case HprofTag::kHeapDumpSegment:
      stats_.heap_dump_count++;
      return ParseHeapDump(length);

    case HprofTag::kHeapDumpEnd:
      // Nothing to do for this tag
      return true;

    case HprofTag::kFrame:
    case HprofTag::kTrace:
      // Just skip these records
      return iterator_->SkipBytes(length);
  }

  // Skip unknown tags
  return iterator_->SkipBytes(length);
}

bool HeapGraphBuilder::ParseUtf8StringRecord(uint32_t length) {
  uint64_t id;
  if (!iterator_->ReadId(id, header_.GetIdSize())) {
    return false;
  }

  std::string str;
  if (!iterator_->ReadString(str, length - header_.GetIdSize())) {
    return false;
  }

  StoreString(id, str);
  stats_.string_count++;
  return true;
}

bool HeapGraphBuilder::ParseClassDefinition() {
  uint32_t serial_num;
  if (!iterator_->ReadU4(serial_num))
    return false;

  uint64_t class_obj_id;
  if (!iterator_->ReadId(class_obj_id, header_.GetIdSize()))
    return false;

  uint32_t stack_trace;
  if (!iterator_->ReadU4(stack_trace))
    return false;

  uint64_t name_id;
  if (!iterator_->ReadId(name_id, header_.GetIdSize()))
    return false;

  // Get class name from strings map and normalize.
  std::string class_name = NormalizeClassName(LookupString(name_id));

  ClassDefinition class_def(class_obj_id, class_name);
  classes_[class_obj_id] = class_def;
  stats_.class_count++;

  for (const auto& [type_name, field_type] : kPrimitiveArrayTypes) {
    if (class_name == type_name) {
      prim_array_class_ids_[static_cast<size_t>(field_type)] = class_obj_id;
      break;  // Found the match, no need to continue searching
    }
  }

  return true;
}

bool HeapGraphBuilder::ParseHeapDump(size_t length) {
  size_t end_position = iterator_->GetPosition() + length;

  // Parse heap dump records until we reach the end of the segment
  while (iterator_->GetPosition() < end_position) {
    if (!ParseHeapDumpRecord()) {
      return false;
    }
  }

  // Ensure we're at the exact end position
  if (iterator_->GetPosition() != end_position) {
    size_t current = iterator_->GetPosition();
    if (current < end_position) {
      // Skip any remaining bytes
      iterator_->SkipBytes(end_position - current);
    } else {
      // We went too far, which is an error
      return false;
    }
  }

  return true;
}

bool HeapGraphBuilder::ParseHeapDumpRecord() {
  // Read sub-record type
  uint8_t tag_value;
  if (!iterator_->ReadU1(tag_value)) {
    return false;
  }

  // First check if it's a root record by looking at the tag value
  bool is_heap_record =
      (tag_value == static_cast<uint8_t>(HprofHeapTag::kClassDump) ||
       tag_value == static_cast<uint8_t>(HprofHeapTag::kInstanceDump) ||
       tag_value == static_cast<uint8_t>(HprofHeapTag::kObjArrayDump) ||
       tag_value == static_cast<uint8_t>(HprofHeapTag::kPrimArrayDump) ||
       tag_value == static_cast<uint8_t>(HprofHeapTag::kHeapDumpInfo));

  if (!is_heap_record) {
    // Assuming it's a root record
    HprofHeapRootTag root_tag = static_cast<HprofHeapRootTag>(tag_value);
    return ParseRootRecord(root_tag);
  }

  // Handle heap records
  switch (static_cast<HprofHeapTag>(tag_value)) {
    case HprofHeapTag::kClassDump:
      return ParseClassStructure();
    case HprofHeapTag::kInstanceDump:
      return ParseInstanceObject();
    case HprofHeapTag::kObjArrayDump:
      return ParseObjectArrayObject();
    case HprofHeapTag::kPrimArrayDump:
      return ParsePrimitiveArrayObject();
    case HprofHeapTag::kHeapDumpInfo:
      return ParseHeapName();
  }

  // This should be unreachable given the logic above, but keeping it for safety
  context_->storage->IncrementStats(stats::hprof_heap_dump_counter);
  return false;
}

bool HeapGraphBuilder::ParseRootRecord(HprofHeapRootTag tag) {
  uint64_t object_id;
  if (!iterator_->ReadId(object_id, header_.GetIdSize())) {
    return false;
  }

  switch (tag) {
    case HprofHeapRootTag::kJniGlobal:
      if (!iterator_->SkipBytes(header_.GetIdSize()))
        return false;
      break;

    case HprofHeapRootTag::kJniLocal:
    case HprofHeapRootTag::kJavaFrame:
    case HprofHeapRootTag::kJniMonitor:
      if (!iterator_->SkipBytes(8))  // thread serial + frame index
        return false;
      break;

    case HprofHeapRootTag::kNativeStack:
    case HprofHeapRootTag::kThreadBlock:
      if (!iterator_->SkipBytes(4))  // thread serial
        return false;
      break;

    case HprofHeapRootTag::kThreadObj:
      if (!iterator_->SkipBytes(8))  // thread serial + stack trace serial
        return false;
      break;

    case HprofHeapRootTag::kStickyClass:
    case HprofHeapRootTag::kMonitorUsed:
    case HprofHeapRootTag::kInternedString:
    case HprofHeapRootTag::kFinalizing:
    case HprofHeapRootTag::kDebugger:
    case HprofHeapRootTag::kVmInternal:
    case HprofHeapRootTag::kUnknown:
      // Most others have no extra data
      break;
  }

  stats_.root_count++;
  roots_[object_id] = tag;
  return true;
}

bool HeapGraphBuilder::ParseClassStructure() {
  uint64_t class_id;
  if (!iterator_->ReadId(class_id, header_.GetIdSize()))
    return false;

  uint32_t stack_trace;
  if (!iterator_->ReadU4(stack_trace))
    return false;

  uint64_t super_class_id;
  if (!iterator_->ReadId(super_class_id, header_.GetIdSize()))
    return false;

  uint64_t class_loader_id, signers_id, protection_domain_id;
  if (!iterator_->ReadId(class_loader_id, header_.GetIdSize()))
    return false;
  if (!iterator_->ReadId(signers_id, header_.GetIdSize()))
    return false;
  if (!iterator_->ReadId(protection_domain_id, header_.GetIdSize()))
    return false;

  // Reserved (2 IDs)
  uint64_t reserved1, reserved2;
  if (!iterator_->ReadId(reserved1, header_.GetIdSize()))
    return false;
  if (!iterator_->ReadId(reserved2, header_.GetIdSize()))
    return false;

  uint32_t instance_size;
  if (!iterator_->ReadU4(instance_size))
    return false;

  // Get class definition
  auto cls = classes_.Find(class_id);
  if (!cls) {
    context_->storage->IncrementStats(stats::hprof_class_errors);
    return false;
  }

  cls->SetSuperClassId(super_class_id);
  cls->SetInstanceSize(instance_size);

  // Constant pool (ignored)
  uint16_t constant_pool_size;
  if (!iterator_->ReadU2(constant_pool_size))
    return false;
  for (uint16_t i = 0; i < constant_pool_size; ++i) {
    uint16_t index;
    uint8_t type_value;
    if (!iterator_->ReadU2(index))
      return false;
    if (!iterator_->ReadU1(type_value))
      return false;
    FieldType type = static_cast<FieldType>(type_value);
    size_t size = GetFieldTypeSize(type, header_.GetIdSize());
    if (!iterator_->SkipBytes(size))
      return false;
  }

  // Static fields
  // Ensure the class object exists in the heap graph
  Object& class_obj = objects_[class_id];
  if (class_obj.GetId() == 0) {
    class_obj = Object(class_id, class_id, current_heap_, ObjectType::kClass);
    class_obj.SetHeapType(current_heap_);
  }

  uint16_t static_field_count;
  if (!iterator_->ReadU2(static_field_count))
    return false;

  for (uint16_t i = 0; i < static_field_count; ++i) {
    uint64_t name_id;
    uint8_t type_value;

    if (!iterator_->ReadId(name_id, header_.GetIdSize()))
      return false;
    if (!iterator_->ReadU1(type_value))
      return false;

    FieldType field_type = static_cast<FieldType>(type_value);
    std::string field_name = LookupString(name_id);

    switch (field_type) {
      case FieldType::kObject: {
        uint64_t target_id = 0;
        if (!iterator_->ReadId(target_id, header_.GetIdSize()))
          return false;
        class_obj.AddPendingReference(field_name, std::nullopt, target_id);
        Field field(field_name, field_type, target_id);
        class_obj.AddField(std::move(field));
        break;
      }
      case FieldType::kBoolean:
      case FieldType::kByte: {
        uint8_t value;
        if (!iterator_->ReadU1(value))
          return false;
        Field field(field_name, field_type, value);
        class_obj.AddField(std::move(field));
        break;
      }
      case FieldType::kChar:
      case FieldType::kShort: {
        uint16_t value;
        if (!iterator_->ReadU2(value))
          return false;
        Field field(field_name, field_type, value);
        class_obj.AddField(std::move(field));
        break;
      }
      case FieldType::kFloat: {
        uint32_t raw_value;
        if (!iterator_->ReadU4(raw_value))
          return false;
        float value;
        memcpy(&value, &raw_value, sizeof(float));
        Field field(field_name, field_type, value);
        class_obj.AddField(std::move(field));
        break;
      }
      case FieldType::kInt: {
        uint32_t value;
        if (!iterator_->ReadU4(value))
          return false;
        Field field(field_name, field_type, static_cast<int32_t>(value));
        class_obj.AddField(std::move(field));
        break;
      }
      case FieldType::kDouble: {
        uint32_t high, low;
        if (!iterator_->ReadU4(high) || !iterator_->ReadU4(low))
          return false;
        uint64_t raw_value = (static_cast<uint64_t>(high) << 32) | low;
        double value;
        memcpy(&value, &raw_value, sizeof(double));
        Field field(field_name, field_type, value);
        class_obj.AddField(std::move(field));
        break;
      }
      case FieldType::kLong: {
        uint32_t high, low;
        if (!iterator_->ReadU4(high) || !iterator_->ReadU4(low))
          return false;
        uint64_t value = (static_cast<uint64_t>(high) << 32) | low;
        Field field(field_name, field_type, static_cast<int64_t>(value));
        class_obj.AddField(std::move(field));
        break;
      }
    }
  }

  // Instance fields
  uint16_t instance_field_count;
  if (!iterator_->ReadU2(instance_field_count))
    return false;

  std::vector<Field> fields;
  fields.reserve(instance_field_count);

  for (uint16_t i = 0; i < instance_field_count; ++i) {
    uint64_t name_id;
    uint8_t type_value;
    if (!iterator_->ReadId(name_id, header_.GetIdSize()))
      return false;
    if (!iterator_->ReadU1(type_value))
      return false;

    std::string field_name = LookupString(name_id);
    fields.emplace_back(field_name, static_cast<FieldType>(type_value));
  }

  cls->SetInstanceFields(std::move(fields));
  return true;
}

bool HeapGraphBuilder::ParseInstanceObject() {
  uint64_t object_id;
  if (!iterator_->ReadId(object_id, header_.GetIdSize())) {
    return false;
  }

  uint32_t stack_trace;
  if (!iterator_->ReadU4(stack_trace)) {
    return false;
  }

  uint64_t class_id;
  if (!iterator_->ReadId(class_id, header_.GetIdSize())) {
    return false;
  }

  uint32_t data_length;
  if (!iterator_->ReadU4(data_length)) {
    return false;
  }

  std::vector<uint8_t> data;
  if (!iterator_->ReadBytes(data, data_length)) {
    return false;
  }

  // Preserve root metadata if this object was already seen as a root
  bool was_root = false;
  std::optional<HprofHeapRootTag> root_type;

  auto it = objects_.Find(object_id);
  if (it) {
    was_root = it->IsRoot();
    root_type = it->GetRootType();
  }

  // Overwrite or create object
  Object obj(object_id, class_id, current_heap_, ObjectType::kInstance);
  obj.SetRawData(std::move(data));
  obj.SetHeapType(current_heap_);

  if (was_root && root_type.has_value()) {
    obj.SetRootType(root_type.value());
  }

  objects_[object_id] = std::move(obj);
  stats_.instance_count++;
  return true;
}

bool HeapGraphBuilder::ParseObjectArrayObject() {
  uint64_t array_id;
  if (!iterator_->ReadId(array_id, header_.GetIdSize())) {
    return false;
  }

  uint32_t stack_trace;
  if (!iterator_->ReadU4(stack_trace)) {
    return false;
  }

  uint32_t element_count;
  if (!iterator_->ReadU4(element_count)) {
    return false;
  }

  uint64_t array_class_id;
  if (!iterator_->ReadId(array_class_id, header_.GetIdSize())) {
    return false;
  }

  // Read elements
  std::vector<uint64_t> elements;
  elements.reserve(element_count);

  for (uint32_t i = 0; i < element_count; i++) {
    uint64_t element_id;
    if (!iterator_->ReadId(element_id, header_.GetIdSize())) {
      return false;
    }
    elements.push_back(element_id);
  }

  Object obj{array_id, array_class_id, current_heap_, ObjectType::kObjectArray};
  obj.SetArrayElements(std::move(elements));
  obj.SetArrayElementType(FieldType::kObject);
  obj.SetHeapType(current_heap_);

  objects_[array_id] = std::move(obj);
  stats_.object_array_count++;

  return true;
}

bool HeapGraphBuilder::ParsePrimitiveArrayObject() {
  uint64_t array_id;
  if (!iterator_->ReadId(array_id, header_.GetIdSize())) {
    return false;
  }

  uint32_t stack_trace;
  if (!iterator_->ReadU4(stack_trace)) {
    return false;
  }

  uint32_t element_count;
  if (!iterator_->ReadU4(element_count)) {
    return false;
  }

  uint8_t element_type_value;
  if (!iterator_->ReadU1(element_type_value)) {
    return false;
  }
  FieldType element_type = static_cast<FieldType>(element_type_value);

  size_t type_size = GetFieldTypeSize(element_type, header_.GetIdSize());

  std::vector<uint8_t> data;
  if (!iterator_->ReadBytes(data, element_count * type_size)) {
    return false;
  }

  uint64_t class_id = 0;
  size_t element_type_index = static_cast<size_t>(element_type);
  if (element_type_index >= prim_array_class_ids_.size()) {
    context_->storage->IncrementStats(
        stats::hprof_primitive_array_parsing_errors);
    return false;
  } else {
    class_id = prim_array_class_ids_[element_type_index];
    if (class_id == 0) {
      context_->storage->IncrementStats(
          stats::hprof_primitive_array_parsing_errors);
      return false;
    }
  }

  Object obj{array_id, class_id, current_heap_, ObjectType::kPrimitiveArray};
  obj.SetRawData(std::move(data));
  obj.SetArrayElementType(element_type);
  obj.SetHeapType(current_heap_);

  objects_[array_id] = std::move(obj);
  stats_.primitive_array_count++;

  return true;
}

bool HeapGraphBuilder::ParseHeapName() {
  uint32_t heap_id;
  if (!iterator_->ReadU4(heap_id)) {
    return false;
  }

  uint64_t name_string_id;
  if (!iterator_->ReadId(name_string_id, header_.GetIdSize())) {
    return false;
  }

  current_heap_ = LookupString(name_string_id);
  return true;
}

std::string HeapGraphBuilder::LookupString(uint64_t id) const {
  auto it = strings_.Find(id);
  if (!it) {
    return "[unknown string ID: " + std::to_string(id) + "]";
  }
  return context_->storage->GetString(*it).c_str();
}

void HeapGraphBuilder::StoreString(uint64_t id, const std::string& str) {
  StringId interned_id = context_->storage->InternString(str);
  strings_[id] = interned_id;
}

// ART outputs class names such as:
//   "java.lang.Class", "java.lang.Class[]", "byte", "byte[]"
// RI outputs class names such as:
//   "java/lang/Class", '[Ljava/lang/Class;", N/A, "[B"
//
// This function converts all class names to match the ART format, which is
// assumed elsewhere in ahat.
// See: ahat/java/com/android/ahat/heapdump/Parser.java
std::string HeapGraphBuilder::NormalizeClassName(
    const std::string& name) const {
  // Count the number of array dimensions
  int num_dimensions = 0;
  std::string normalized_name = name;

  while (!normalized_name.empty() && normalized_name[0] == '[') {
    num_dimensions++;
    normalized_name = normalized_name.substr(1);
  }

  if (num_dimensions > 0) {
    // If there was an array type signature to start, then interpret the
    // class name as a type signature.
    if (normalized_name.empty()) {
      context_->storage->IncrementStats(stats::hprof_class_errors);
      return name;
    }

    char type_char = normalized_name[0];
    switch (type_char) {
      case 'Z':
        normalized_name = "boolean";
        break;
      case 'B':
        normalized_name = "byte";
        break;
      case 'C':
        normalized_name = "char";
        break;
      case 'S':
        normalized_name = "short";
        break;
      case 'I':
        normalized_name = "int";
        break;
      case 'J':
        normalized_name = "long";
        break;
      case 'F':
        normalized_name = "float";
        break;
      case 'D':
        normalized_name = "double";
        break;
      case 'L':
        // Remove the leading 'L' and trailing ';'
        if (normalized_name.back() != ';') {
          context_->storage->IncrementStats(stats::hprof_class_errors);
          return name;
        }
        normalized_name =
            normalized_name.substr(1, normalized_name.length() - 2);
        break;
      default:
        context_->storage->IncrementStats(stats::hprof_class_errors);
        return name;
    }
  }

  // Replace forward slashes with dots
  std::replace(normalized_name.begin(), normalized_name.end(), '/', '.');

  // Add back array dimensions
  for (int i = 0; i < num_dimensions; ++i) {
    normalized_name += "[]";
  }

  return normalized_name;
}
}  // namespace perfetto::trace_processor::art_hprof
