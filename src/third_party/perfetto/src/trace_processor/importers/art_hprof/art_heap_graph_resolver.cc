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

#include "perfetto/ext/base/flat_hash_map.h"
#include "src/trace_processor/importers/art_hprof/art_heap_graph_builder.h"

#include <unordered_set>
namespace perfetto::trace_processor::art_hprof {

template <typename T>
T ReadBigEndian(TraceProcessorContext* context,
                const std::vector<uint8_t>& data,
                size_t offset,
                size_t length) {
  if (offset + length > data.size()) {
    context->storage->IncrementStats(stats::hprof_field_value_errors);
    return 0;
  }

  T result = 0;
  for (size_t i = 0; i < length; ++i) {
    result = static_cast<T>((result << 8) | static_cast<T>(data[offset + i]));
  }
  return result;
}

template <typename T>
T ReadBigEndian(TraceProcessorContext* context,
                const std::vector<uint8_t>& data,
                size_t offset) {
  return ReadBigEndian<T>(context, data, offset, sizeof(T));
}

template <typename T>
void ExtractTypedArrayValues(TraceProcessorContext* context,
                             Object& obj,
                             const std::vector<uint8_t>& data,
                             size_t element_count,
                             size_t element_size) {
  std::vector<T> values;
  values.reserve(element_count);

  for (size_t i = 0; i < element_count; ++i) {
    size_t offset = i * element_size;
    T value = ReadBigEndian<T>(context, data, offset);
    values.push_back(value);
  }

  obj.SetArrayData(std::move(values));
}

HeapGraphResolver::HeapGraphResolver(
    TraceProcessorContext* context,
    HprofHeader& header,
    base::FlatHashMap<uint64_t, Object>& objects,
    base::FlatHashMap<uint64_t, ClassDefinition>& classes,
    base::FlatHashMap<uint64_t, HprofHeapRootTag>& roots,
    DebugStats& stats)
    : context_(context),
      header_(header),
      objects_(objects),
      roots_(roots),
      classes_(classes),
      stats_(stats) {}

void HeapGraphResolver::ResolveGraph() {
  // Extract field values and references for all objects
  ExtractAllObjectData();

  // Mark reachability from roots
  MarkReachableObjects();

  // Set native_size for native objects. See:
  CalculateNativeSizes();
}

void HeapGraphResolver::ExtractAllObjectData() {
  for (auto it = objects_.GetIterator(); it; ++it) {
    auto& obj = it.value();
    // Extract data based on object type
    if (obj.GetObjectType() == ObjectType::kInstance ||
        obj.GetObjectType() == ObjectType::kClass) {
      auto cls = classes_.Find(obj.GetClassId());
      if (cls) {
        ExtractObjectReferences(obj, *cls);
        ExtractFieldValues(obj, *cls);
      }
    } else if (obj.GetObjectType() == ObjectType::kObjectArray) {
      ExtractArrayElementReferences(obj);
    } else if (obj.GetObjectType() == ObjectType::kPrimitiveArray) {
      ExtractPrimitiveArrayValues(obj);
    }

    uint64_t obj_id = obj.GetId();
    auto pending = roots_.Find(obj_id);
    if (pending) {
      obj.SetRootType(*pending);
      roots_.Erase(obj_id);
    }
  }
}

void HeapGraphResolver::MarkReachableObjects() {
  std::unordered_set<uint64_t> visited;
  std::vector<uint64_t> processing_stack;

  // Add all root objects to the stack
  for (auto it = objects_.GetIterator(); it; ++it) {
    auto id = it.key();
    auto& obj = it.value();
    if (obj.IsRoot()) {
      processing_stack.push_back(id);
      obj.SetReachable();
    }
  }

  // Process reachability
  while (!processing_stack.empty()) {
    uint64_t current_id = processing_stack.back();
    processing_stack.pop_back();

    // Skip if already visited
    if (!visited.insert(current_id).second)
      continue;

    auto& obj = objects_[current_id];

    // Add reference targets to stack and mark them as reachable
    for (const auto& ref : obj.GetReferences()) {
      auto it = objects_.Find(ref.target_id);
      if (it && !it->IsReachable()) {
        // Mark target as reachable
        it->SetReachable();

        // Add to processing stack
        processing_stack.push_back(ref.target_id);
      }
    }
  }
}

void HeapGraphResolver::ExtractArrayElementReferences(Object& obj) {
  const auto& elements = obj.GetArrayElements();
  for (size_t i = 0; i < elements.size(); ++i) {
    uint64_t element_id = elements[i];
    if (element_id != 0) {
      std::string ref_name = "[" + std::to_string(i) + "]";
      auto owned_obj = objects_.Find(element_id);
      if (owned_obj) {
        obj.AddReference(ref_name, owned_obj->GetClassId(), element_id);
        stats_.reference_count++;
      }
    }
  }
}

bool HeapGraphResolver::ExtractObjectReferences(Object& obj,
                                                const ClassDefinition& cls) {
  // Handle static fields of class objects. Now that we have all objects and
  // classes available
  for (const auto& ref : obj.GetPendingReferences()) {
    if (!ref.field_class_id) {
      auto it = objects_.Find(ref.target_id);
      if (it) {
        obj.AddReference(ref.field_name, it->GetClassId(), ref.target_id);
        stats_.reference_count++;
      }
    }
  }

  const std::vector<uint8_t>& data = obj.GetRawData();
  if (data.empty()) {
    return true;
  }

  std::vector<Field> fields = GetClassHierarchyFields(cls.GetId());
  size_t offset = 0;

  for (const auto& field : fields) {
    if (offset >= data.size()) {
      break;
    }

    if (field.GetType() == FieldType::kObject) {
      // Make sure we have enough data to read the ID
      if (offset + header_.GetIdSize() <= data.size()) {
        // Use the helper function consistently for all ID extractions
        uint64_t target_id = ReadBigEndian<uint64_t>(context_, data, offset,
                                                     header_.GetIdSize());
        offset += header_.GetIdSize();

        if (target_id != 0) {
          uint64_t field_class_id = 0;
          auto it = objects_.Find(target_id);
          if (it) {
            field_class_id = it->GetClassId();
          }

          obj.AddReference(field.GetName(), field_class_id, target_id);
          stats_.reference_count++;
        }
      } else {
        context_->storage->IncrementStats(stats::hprof_reference_errors);
        break;
      }
    } else {
      offset += field.GetSize();
    }
  }

  return true;
}

void HeapGraphResolver::ExtractFieldValues(Object& obj,
                                           const ClassDefinition& cls) {
  if (obj.GetObjectType() != ObjectType::kInstance ||
      obj.GetRawData().empty()) {
    return;
  }

  // Get all fields for the class hierarchy
  std::vector<Field> fields = GetClassHierarchyFields(cls.GetId());

  // Parse the raw data to extract field values
  size_t offset = 0;
  for (const auto& field_def : fields) {
    // Skip if we've run out of data
    if (offset >= obj.GetRawData().size()) {
      break;
    }

    // Create a field with the same name and type
    Field field(field_def.GetName(), field_def.GetType());

    // Extract the value based on type
    const auto& data = obj.GetRawData();
    switch (field_def.GetType()) {
      case FieldType::kBoolean: {
        bool value = data[offset] != 0;
        field.SetValue(value);
        offset += 1;
        break;
      }
      case FieldType::kByte: {
        uint8_t value = data[offset];
        field.SetValue(value);
        offset += 1;
        break;
      }
      case FieldType::kChar: {
        field.SetValue(ReadBigEndian<uint16_t>(context_, data, offset));
        offset += 2;
        break;
      }
      case FieldType::kShort: {
        field.SetValue(ReadBigEndian<int16_t>(context_, data, offset));
        offset += 2;
        break;
      }
      case FieldType::kInt: {
        field.SetValue(ReadBigEndian<int32_t>(context_, data, offset));
        offset += 4;
        break;
      }
      case FieldType::kLong: {
        field.SetValue(ReadBigEndian<int64_t>(context_, data, offset));
        offset += 8;
        break;
      }
      case FieldType::kFloat: {
        uint32_t raw = ReadBigEndian<uint32_t>(context_, data, offset);
        float value;
        std::memcpy(&value, &raw, sizeof(float));
        field.SetValue(value);
        offset += 4;
        break;
      }
      case FieldType::kDouble: {
        uint64_t raw = ReadBigEndian<uint64_t>(context_, data, offset);
        double value;
        std::memcpy(&value, &raw, sizeof(double));
        field.SetValue(value);
        offset += 8;
        break;
      }
      case FieldType::kObject: {
        // Object IDs are based on the ID size
        uint64_t id = ReadBigEndian<uint64_t>(context_, data, offset,
                                              header_.GetIdSize());
        field.SetValue(id);
        offset += header_.GetIdSize();
        break;
      }
    }

    if (auto str = DecodeJavaString(obj)) {
      field.SetDecodedString(*str);
    }

    // Add the field with its value to the object
    obj.AddField(std::move(field));
  }
}

void HeapGraphResolver::ExtractPrimitiveArrayValues(Object& obj) {
  if (obj.GetObjectType() != ObjectType::kPrimitiveArray ||
      obj.GetRawData().empty()) {
    return;
  }

  const FieldType element_type = obj.GetArrayElementType();
  const auto& data = obj.GetRawData();
  size_t element_size = GetFieldTypeSize(element_type, header_.GetIdSize());

  // Skip if the data is invalid
  if (element_size == 0 || data.size() % element_size != 0) {
    return;
  }

  // Calculate the number of elements
  size_t element_count = data.size() / element_size;

  // Parse the array based on its element type
  switch (element_type) {
    case FieldType::kBoolean: {
      std::vector<bool> values;
      values.reserve(element_count);
      for (size_t i = 0; i < element_count; ++i) {
        values.push_back(data[i] != 0);
      }
      obj.SetArrayData(std::move(values));
      break;
    }
    case FieldType::kByte: {
      // For byte arrays, we can directly use the raw data
      std::vector<uint8_t> values(data.begin(), data.end());
      obj.SetArrayData(std::move(values));
      break;
    }
    case FieldType::kChar:
      ExtractTypedArrayValues<uint16_t>(context_, obj, data, element_count,
                                        element_size);
      break;
    case FieldType::kShort:
      ExtractTypedArrayValues<int16_t>(context_, obj, data, element_count,
                                       element_size);
      break;
    case FieldType::kInt:
      ExtractTypedArrayValues<int32_t>(context_, obj, data, element_count,
                                       element_size);
      break;
    case FieldType::kLong:
      ExtractTypedArrayValues<int64_t>(context_, obj, data, element_count,
                                       element_size);
      break;
    case FieldType::kFloat: {
      std::vector<float> values;
      values.reserve(element_count);
      for (size_t i = 0; i < element_count; ++i) {
        size_t offset = i * element_size;
        uint32_t raw = ReadBigEndian<uint32_t>(context_, data, offset);
        float value;
        memcpy(&value, &raw, sizeof(float));
        values.push_back(value);
      }
      obj.SetArrayData(std::move(values));
      break;
    }
    case FieldType::kDouble: {
      std::vector<double> values;
      values.reserve(element_count);
      for (size_t i = 0; i < element_count; ++i) {
        size_t offset = i * element_size;
        uint64_t raw = ReadBigEndian<uint64_t>(context_, data, offset);
        double value;
        memcpy(&value, &raw, sizeof(double));
        values.push_back(value);
      }
      obj.SetArrayData(std::move(values));
      break;
    }
    case FieldType::kObject:
      // Object arrays should be handled by HandleObjectArrayDumpRecord
      break;
  }
}

std::optional<std::string> HeapGraphResolver::DecodeJavaString(
    const Object& string_obj) const {
  // 1. Verify it's a java.lang.String object
  auto cls = classes_.Find(string_obj.GetClassId());
  if (!cls || cls->GetName() != kJavaLangString)
    return std::nullopt;

  uint64_t value_array_id = 0;
  std::optional<int32_t> offset_opt;
  std::optional<int32_t> count_opt;
  std::optional<uint8_t> coder_opt;

  // 2. Extract fields: value, offset, count, coder
  for (const Field& f : string_obj.GetFields()) {
    if (f.GetName() == "value") {
      if (auto v = f.GetValue<uint64_t>())
        value_array_id = *v;
    } else if (f.GetName() == "offset") {
      offset_opt = f.GetValue<int32_t>();
    } else if (f.GetName() == "count") {
      count_opt = f.GetValue<int32_t>();
    } else if (f.GetName() == "coder") {
      coder_opt = f.GetValue<uint8_t>();
    }
  }

  if (value_array_id == 0)
    return std::nullopt;

  // 3. Get the backing array
  auto array = objects_.Find(value_array_id);
  if (!array)
    return std::nullopt;

  size_t array_len = array->GetArrayElementCount();
  int32_t offset = offset_opt.value_or(0);
  int32_t count = count_opt.value_or(static_cast<int32_t>(array_len) - offset);

  if (offset < 0 || count < 0 ||
      static_cast<size_t>(offset + count) > array_len)
    return std::nullopt;

  // 4. Decode string
  std::string result;
  result.reserve(static_cast<size_t>(count));

  auto append_utf8_from_utf16 = [&](uint16_t ch) {
    if (ch < 0x80) {
      result.push_back(static_cast<char>(ch));
    } else if (ch < 0x800) {
      result.push_back(static_cast<char>(0xC0 | (ch >> 6)));
      result.push_back(static_cast<char>(0x80 | (ch & 0x3F)));
    } else {
      result.push_back(static_cast<char>(0xE0 | (ch >> 12)));
      result.push_back(static_cast<char>(0x80 | ((ch >> 6) & 0x3F)));
      result.push_back(static_cast<char>(0x80 | (ch & 0x3F)));
    }
  };

  const auto type = array->GetArrayElementType();

  if (type == FieldType::kByte) {
    const auto& bytes = array->GetArrayData<uint8_t>();
    for (int32_t i = 0; i < count; ++i)
      result.push_back(
          static_cast<char>(bytes[static_cast<size_t>(offset + i)]));
    return result;
  } else if (type == FieldType::kChar) {
    const auto& chars = array->GetArrayData<uint16_t>();
    for (int32_t i = 0; i < count; ++i)
      append_utf8_from_utf16(chars[static_cast<size_t>(offset + i)]);
    return result;
  }

  return std::nullopt;
}

//
//             +-------------------------------+  .referent   +--------+
//             |       sun.misc.Cleaner        | -----------> | Object |
//             +-------------------------------+              +--------+
//                |
//                | .thunk
//                v
// +----------------------------------------------------+
// | libcore.util.NativeAllocationRegistry$CleanerThunk |
// +----------------------------------------------------+
//   |
//   | .this$0
//   v
// +----------------------------------------------------+
// |       libcore.util.NativeAllocationRegistry        |
// |                       .size                        |
// +----------------------------------------------------+
//
// `.size` should be attributed as the native size of Object
// See: perfetto/src/trace_processor/importers/proto/heap_graph_tracker.cc
std::vector<Field> HeapGraphResolver::GetClassHierarchyFields(
    uint64_t class_id) const {
  std::vector<Field> result;

  // Follow class hierarchy to collect all fields
  uint64_t current_class_id = class_id;
  while (current_class_id != 0) {
    auto cls = classes_.Find(current_class_id);
    if (!cls) {
      break;
    }

    const auto& fields = cls->GetInstanceFields();

    // Add fields from this class
    result.insert(result.end(), fields.begin(), fields.end());

    // Move up to superclass
    current_class_id = cls->GetSuperClassId();
  }

  return result;
}

void HeapGraphResolver::CalculateNativeSizes() {
  std::vector<std::pair<uint64_t, uint64_t>>
      cleaners;  // (referent_id, thunk_id)

  // Find sun.misc.Cleaner objects
  for (auto it = objects_.GetIterator(); it; ++it) {
    auto obj_id = it.key();
    auto& obj = it.value();
    auto cls = classes_.Find(obj.GetClassId());
    if (!cls) {
      continue;
    }

    if (cls->GetName() != kSunMiscCleaner) {
      continue;
    }

    std::optional<uint64_t> referent_id;
    std::optional<uint64_t> thunk_id;
    std::optional<uint64_t> next_id;

    for (const auto& ref : obj.GetReferences()) {
      if (ref.field_name == "referent") {
        referent_id = ref.target_id;
      } else if (ref.field_name == "thunk") {
        thunk_id = ref.target_id;
      } else if (ref.field_name == "next") {
        next_id = ref.target_id;
      }
    }

    if (!referent_id || !thunk_id) {
      continue;
    }

    // Skip cleaned Cleaner objects
    if (next_id && *next_id == obj_id) {
      continue;
    }

    cleaners.emplace_back(*referent_id, *thunk_id);
  }

  // Traverse cleaner chains to find NativeAllocationRegistry and attribute size
  for (const auto& [referent_id, thunk_id] : cleaners) {
    auto thunk = objects_.Find(thunk_id);
    if (!thunk) {
      continue;
    }

    std::optional<uint64_t> registry_id;
    for (const auto& ref : thunk->GetReferences()) {
      if (ref.field_name == "this$0") {
        registry_id = ref.target_id;
        break;
      }
    }

    if (!registry_id) {
      continue;
    }

    auto registry = objects_.Find(*registry_id);
    if (!registry) {
      continue;
    }

    auto size_field = registry->FindField("size");
    if (!size_field) {
      continue;
    }

    int64_t native_size = size_field->GetNumericValue();
    if (native_size <= 0) {
      continue;
    }

    auto referent = objects_.Find(referent_id);
    if (referent) {
      referent->AddNativeSize(native_size);
    }
  }
}
}  // namespace perfetto::trace_processor::art_hprof
