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

#include <deque>

#include "perfetto/ext/base/flat_hash_map.h"
#include "src/trace_processor/importers/art_hprof/art_heap_graph_builder.h"

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
  ExtractAllObjectData();
  MarkReachableObjects();
  ComputeSelfSizes();
  CalculateNativeSizes();
}

void HeapGraphResolver::ExtractAllObjectData() {
  // Identify classes whose instances need field extraction (for native size
  // calculation and string decoding). All other instances only need references.
  base::FlatHashMap<uint64_t, bool> needs_field_extraction;
  for (auto it = classes_.GetIterator(); it; ++it) {
    const auto& name = it.value().GetName();
    if (name == "libcore.util.NativeAllocationRegistry" ||
        name == kJavaLangString) {
      needs_field_extraction[it.key()] = true;
    }
  }

  for (auto it = objects_.GetIterator(); it; ++it) {
    auto& obj = it.value();
    if (obj.GetObjectType() == ObjectType::kInstance ||
        obj.GetObjectType() == ObjectType::kClass) {
      auto cls = classes_.Find(obj.GetClassId());
      if (cls) {
        ExtractObjectReferences(obj, *cls);
        if (needs_field_extraction.Find(obj.GetClassId())) {
          ExtractFieldValues(obj, *cls);
        }
      }
    } else if (obj.GetObjectType() == ObjectType::kObjectArray) {
      ExtractArrayElementReferences(obj);
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
  // BFS from roots to mark reachability and compute shortest-path
  // root_distance. Skips "referent" fields from weak/phantom/finalizer
  // reference types to match ahat's retained=SOFT behavior (soft referent
  // edges are followed).

  // Pre-compute which class IDs are reference types by walking the hierarchy.
  base::FlatHashMap<uint64_t, bool> ref_type_classes;
  {
    base::FlatHashMap<uint64_t, bool> base_refs;
    for (auto it = classes_.GetIterator(); it; ++it) {
      const auto& name = it.value().GetName();
      if (name == "java.lang.ref.WeakReference" ||
          name == "java.lang.ref.PhantomReference" ||
          name == "java.lang.ref.FinalizerReference") {
        base_refs[it.key()] = true;
      }
    }
    for (auto it = classes_.GetIterator(); it; ++it) {
      uint64_t current = it.key();
      for (int depth = 0; depth < 100 && current != 0; ++depth) {
        if (base_refs.Find(current)) {
          ref_type_classes[it.key()] = true;
          break;
        }
        auto* cls = classes_.Find(current);
        if (!cls)
          break;
        current = cls->GetSuperClassId();
      }
    }
  }

  std::deque<uint64_t> queue;

  for (auto it = objects_.GetIterator(); it; ++it) {
    auto& obj = it.value();
    if (obj.IsRoot()) {
      queue.push_back(it.key());
      obj.SetReachable();
      obj.SetRootDistance(0);
    }
  }

  while (!queue.empty()) {
    uint64_t current_id = queue.front();
    queue.pop_front();

    auto* obj = objects_.Find(current_id);
    if (!obj) {
      continue;
    }

    bool skip_referent = ref_type_classes.Find(obj->GetClassId()) != nullptr;
    int32_t next_distance = obj->GetRootDistance() + 1;
    for (const auto& ref : obj->GetReferences()) {
      if (skip_referent &&
          ref.field_name == "java.lang.ref.Reference.referent") {
        continue;
      }
      auto* target = objects_.Find(ref.target_id);
      if (target && !target->IsReachable()) {
        target->SetReachable();
        target->SetRootDistance(next_distance);
        queue.push_back(ref.target_id);
      }
    }
  }
}

void HeapGraphResolver::ExtractArrayElementReferences(Object& obj) {
  const auto& elements = obj.GetArrayElements();
  char buf[24];
  for (size_t i = 0; i < elements.size(); ++i) {
    uint64_t element_id = elements[i];
    if (element_id != 0) {
      auto owned_obj = objects_.Find(element_id);
      if (owned_obj) {
        snprintf(buf, sizeof(buf), "[%zu]", i);
        obj.AddReference(buf, owned_obj->GetClassId(), element_id);
        stats_.reference_count++;
      }
    }
  }
}

bool HeapGraphResolver::ExtractObjectReferences(Object& obj,
                                                const ClassDefinition& cls) {
  // Resolve pending static field references, qualifying names as
  // "ClassName.fieldName" to match the proto heap graph format.
  for (const auto& ref : obj.GetPendingReferences()) {
    if (!ref.field_class_id) {
      auto it = objects_.Find(ref.target_id);
      if (it) {
        std::string qualified = cls.GetName() + "." + ref.field_name;
        obj.AddReference(qualified, it->GetClassId(), ref.target_id);
        stats_.reference_count++;
      }
    }
  }

  const std::vector<uint8_t>& data = obj.GetRawData();
  if (data.empty()) {
    return true;
  }

  const auto& fields = GetClassHierarchyFields(cls.GetId());
  size_t offset = 0;

  for (const auto& field : fields) {
    if (offset >= data.size()) {
      break;
    }

    if (field.GetType() == FieldType::kObject) {
      if (offset + header_.GetIdSize() <= data.size()) {
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
      offset += GetFieldTypeSize(field.GetType(), header_.GetIdSize());
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

  const auto& fields = GetClassHierarchyFields(cls.GetId());
  const auto& data = obj.GetRawData();
  size_t offset = 0;
  for (const auto& field_def : fields) {
    if (offset >= data.size()) {
      break;
    }

    Field field(field_def.GetName(), field_def.GetType());
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
        uint64_t id = ReadBigEndian<uint64_t>(context_, data, offset,
                                              header_.GetIdSize());
        field.SetValue(id);
        offset += header_.GetIdSize();
        break;
      }
    }

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

  if (element_size == 0 || data.size() % element_size != 0) {
    return;
  }

  size_t element_count = data.size() / element_size;

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
      break;
  }
}

std::optional<std::string> HeapGraphResolver::DecodeJavaString(
    const Object& string_obj) const {
  auto cls = classes_.Find(string_obj.GetClassId());
  if (!cls || cls->GetName() != kJavaLangString)
    return std::nullopt;

  uint64_t value_array_id = 0;
  std::optional<int32_t> offset_opt;
  std::optional<int32_t> count_opt;

  for (const Field& f : string_obj.GetFields()) {
    if (f.GetName() == "java.lang.String.value") {
      if (auto v = f.GetValue<uint64_t>())
        value_array_id = *v;
    } else if (f.GetName() == "java.lang.String.offset") {
      offset_opt = f.GetValue<int32_t>();
    } else if (f.GetName() == "java.lang.String.count") {
      count_opt = f.GetValue<int32_t>();
    }
  }

  if (value_array_id == 0)
    return std::nullopt;

  auto array = objects_.Find(value_array_id);
  if (!array)
    return std::nullopt;

  size_t array_len = array->GetArrayElementCount();
  int32_t offset = offset_opt.value_or(0);
  int32_t count = count_opt.value_or(static_cast<int32_t>(array_len) - offset);

  if (offset < 0 || count < 0 ||
      static_cast<size_t>(offset + count) > array_len)
    return std::nullopt;

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

const std::vector<Field>& HeapGraphResolver::GetClassHierarchyFields(
    uint64_t class_id) {
  auto* cached = field_cache_.Find(class_id);
  if (cached) {
    return *cached;
  }

  // HPROF instance data is laid out derived-class-first: the most-derived
  // class's fields come first, then its superclass fields, etc.
  // Field names are qualified as "ClassName.fieldName" to match the proto
  // heap graph format and allow MarkReachableObjects to recognize
  // "java.lang.ref.Reference.referent".
  std::vector<Field> result;
  uint64_t current_class_id = class_id;
  while (current_class_id != 0) {
    auto cls = classes_.Find(current_class_id);
    if (!cls) {
      break;
    }
    for (const auto& f : cls->GetInstanceFields()) {
      result.emplace_back(cls->GetName() + "." + f.GetName(), f.GetType());
    }
    current_class_id = cls->GetSuperClassId();
  }

  field_cache_[class_id] = std::move(result);
  return *field_cache_.Find(class_id);
}

void HeapGraphResolver::ComputeSelfSizes() {
  // Match ahat's self_size computation:
  //   Instances: CLASS_DUMP.instanceSize (not INSTANCE_DUMP.data_length).
  //   Class objects: java.lang.Class.instanceSize + staticFieldsSize.
  //   Arrays: CLASS_DUMP.instanceSize (header) + element_count * element_size.
  std::optional<uint32_t> java_lang_class_size;
  for (auto it = classes_.GetIterator(); it; ++it) {
    if (it.value().GetName() == "java.lang.Class") {
      java_lang_class_size = it.value().GetInstanceSize();
      break;
    }
  }

  for (auto it = objects_.GetIterator(); it; ++it) {
    auto& obj = it.value();
    if (obj.GetObjectType() == ObjectType::kClass) {
      // java.lang.Class.instanceSize + sum of static field sizes.
      size_t size = java_lang_class_size.value_or(0);
      for (const auto& field : obj.GetFields()) {
        size += GetFieldTypeSize(field.GetType(), header_.GetIdSize());
      }
      obj.SetSelfSizeOverride(size);
    } else if (obj.GetObjectType() == ObjectType::kInstance) {
      auto* cls = classes_.Find(obj.GetClassId());
      if (cls) {
        obj.SetSelfSizeOverride(cls->GetInstanceSize());
      }
    } else if (obj.GetObjectType() == ObjectType::kObjectArray) {
      auto* cls = classes_.Find(obj.GetClassId());
      if (cls) {
        size_t header = cls->GetInstanceSize();
        size_t data = obj.GetArrayElements().size() * header_.GetIdSize();
        obj.SetSelfSizeOverride(header + data);
      }
    } else if (obj.GetObjectType() == ObjectType::kPrimitiveArray) {
      auto* cls = classes_.Find(obj.GetClassId());
      if (cls) {
        size_t header = cls->GetInstanceSize();
        size_t data = obj.GetRawData().size();
        obj.SetSelfSizeOverride(header + data);
      }
    }
  }
}

// Attribute native_size to objects via the Cleaner→CleanerThunk→Registry chain:
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
// Registry.size is attributed as the native_size of Object.
// Matches ahat's AhatClassInstance.asRegisteredNativeAllocation().
void HeapGraphResolver::CalculateNativeSizes() {
  std::vector<std::pair<uint64_t, uint64_t>>
      cleaners;  // (referent_id, thunk_id)

  // Find sun.misc.Cleaner objects
  for (auto it = objects_.GetIterator(); it; ++it) {
    auto& obj = it.value();
    auto cls = classes_.Find(obj.GetClassId());
    if (!cls || cls->GetName() != kSunMiscCleaner) {
      continue;
    }

    std::optional<uint64_t> referent_id;
    std::optional<uint64_t> thunk_id;

    for (const auto& ref : obj.GetReferences()) {
      if (ref.field_name == "java.lang.ref.Reference.referent") {
        referent_id = ref.target_id;
      } else if (ref.field_name == "sun.misc.Cleaner.thunk") {
        thunk_id = ref.target_id;
      }
    }

    if (!referent_id || !thunk_id) {
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

    // Verify thunk is a CleanerThunk (matches ahat check)
    auto thunk_cls = classes_.Find(thunk->GetClassId());
    if (!thunk_cls ||
        thunk_cls->GetName() != kNativeAllocationRegistryCleanerThunk) {
      continue;
    }

    std::optional<uint64_t> registry_id;
    for (const auto& ref : thunk->GetReferences()) {
      if (ref.field_name ==
          "libcore.util.NativeAllocationRegistry$CleanerThunk.this$0") {
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

    // Verify registry is a NativeAllocationRegistry (matches ahat check)
    auto registry_cls = classes_.Find(registry->GetClassId());
    if (!registry_cls || registry_cls->GetName() != kNativeAllocationRegistry) {
      continue;
    }

    auto size_field =
        registry->FindField("libcore.util.NativeAllocationRegistry.size");
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
