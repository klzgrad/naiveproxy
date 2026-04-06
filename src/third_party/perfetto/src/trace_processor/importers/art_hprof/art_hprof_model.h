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

#ifndef SRC_TRACE_PROCESSOR_IMPORTERS_ART_HPROF_ART_HPROF_MODEL_H_
#define SRC_TRACE_PROCESSOR_IMPORTERS_ART_HPROF_ART_HPROF_MODEL_H_

#include "src/trace_processor/importers/art_hprof/art_hprof_types.h"

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace perfetto::trace_processor::art_hprof {
using ArrayData = std::variant<std::monostate,         // Empty
                               std::vector<bool>,      // Boolean array
                               std::vector<uint8_t>,   // Byte array
                               std::vector<uint16_t>,  // Char array
                               std::vector<int16_t>,   // Short array
                               std::vector<int32_t>,   // Int array
                               std::vector<int64_t>,   // Long array
                               std::vector<float>,     // Float array
                               std::vector<double>,    // Double array
                               std::vector<uint64_t>   // Object array
                               >;

// Field class with value storage using std::variant
class Field {
 public:
  using ValueType = std::variant<std::monostate,  // For no value
                                 bool,            // BOOLEAN
                                 uint8_t,         // BYTE
                                 uint16_t,        // CHAR
                                 int16_t,         // SHORT
                                 int32_t,         // INT
                                 int64_t,         // LONG
                                 float,           // FLOAT
                                 double,          // DOUBLE
                                 uint64_t         // OBJECT (reference ID)
                                 >;

  Field(std::string name, FieldType type)
      : name_(std::move(name)), type_(type) {}

  template <typename T>
  Field(std::string name, FieldType type, T value)
      : name_(std::move(name)), type_(type), value_(value) {}

  Field(const Field&) = default;
  Field& operator=(const Field&) = default;
  Field(Field&&) = default;
  Field& operator=(Field&&) = default;
  ~Field() = default;

  const std::string& GetName() const { return name_; }
  FieldType GetType() const { return type_; }
  bool HasValue() const {
    return !std::holds_alternative<std::monostate>(value_);
  }

  // Template setter for all supported types
  template <typename T>
  void SetValue(T value) {
    value_ = value;
  }

  // Type-safe getter template
  template <typename T>
  std::optional<T> GetValue() const {
    if (const T* ptr = std::get_if<T>(&value_)) {
      return *ptr;
    }
    return std::nullopt;
  }

  // Get numeric value as int64_t (useful for sizes)
  int64_t GetNumericValue() const {
    return std::visit(
        [](auto&& val) -> int64_t {
          using T = std::decay_t<decltype(val)>;
          if constexpr (std::is_same_v<T, std::monostate>)
            return 0;
          else if constexpr (std::is_same_v<T, bool>)
            return val ? 1 : 0;
          else
            return static_cast<int64_t>(val);
        },
        value_);
  }

  void SetDecodedString(std::string str) { decoded_string_ = std::move(str); }
  std::optional<std::string> GetDecodedString() const {
    return decoded_string_;
  }

 private:
  std::string name_;
  FieldType type_;
  ValueType value_ = std::monostate{};
  std::optional<std::string> decoded_string_;
};

struct Reference {
  std::string field_name;
  std::optional<uint64_t> field_class_id;
  uint64_t target_id;

  Reference(std::string_view name,
            std::optional<uint64_t> class_id,
            uint64_t target)
      : field_name(name), field_class_id(class_id), target_id(target) {}
};

class ClassDefinition {
 public:
  ClassDefinition(uint64_t id, std::string name)
      : id_(id), name_(std::move(name)) {}

  ClassDefinition() = default;

  ClassDefinition(const ClassDefinition&) = default;
  ClassDefinition& operator=(const ClassDefinition&) = default;
  ClassDefinition(ClassDefinition&&) = default;
  ClassDefinition& operator=(ClassDefinition&&) = default;
  ~ClassDefinition() = default;

  uint64_t GetId() const { return id_; }
  const std::string& GetName() const { return name_; }
  uint64_t GetSuperClassId() const { return super_class_id_; }
  uint32_t GetInstanceSize() const { return instance_size_; }
  const std::vector<Field>& GetInstanceFields() const {
    return instance_fields_;
  }

  void SetSuperClassId(uint64_t id) { super_class_id_ = id; }
  void SetInstanceSize(uint32_t size) { instance_size_ = size; }
  void SetInstanceFields(std::vector<Field> fields) {
    instance_fields_ = std::move(fields);
  }

  void AddInstanceField(Field field) {
    instance_fields_.push_back(std::move(field));
  }

 private:
  uint64_t id_ = 0;
  std::string name_;
  uint64_t super_class_id_ = 0;
  uint32_t instance_size_ = 0;
  std::vector<Field> instance_fields_;
};

class Object {
 public:
  Object(uint64_t id, uint64_t class_id, std::string heap, ObjectType type)
      : id_(id),
        class_id_(class_id),
        type_(type),
        heap_type_(std::move(heap)) {}

  Object() = default;

  Object(const Object&) = default;
  Object& operator=(const Object&) = default;
  Object(Object&&) = default;
  Object& operator=(Object&&) = default;
  ~Object() = default;

  uint64_t GetId() const { return id_; }
  uint64_t GetClassId() const { return class_id_; }
  const std::string& GetHeapType() const { return heap_type_; }
  ObjectType GetObjectType() const { return type_; }

  void SetRootType(HprofHeapRootTag root_type) {
    root_type_ = root_type;
    is_root_ = true;
  }

  void SetReachable() { is_reachable_ = true; }

  void SetHeapType(std::string heap_type) { heap_type_ = std::move(heap_type); }

  bool IsRoot() const { return is_root_; }
  bool IsReachable() const { return is_reachable_; }
  std::optional<HprofHeapRootTag> GetRootType() const { return root_type_; }

  void SetRootDistance(int32_t d) { root_distance_ = d; }
  int32_t GetRootDistance() const { return root_distance_; }

  void SetRawData(std::vector<uint8_t> data) { raw_data_ = std::move(data); }

  const std::vector<uint8_t>& GetRawData() const { return raw_data_; }

  void AddReference(std::string_view field_name,
                    std::optional<uint64_t> field_class_id,
                    uint64_t target_id) {
    references_.emplace_back(field_name, field_class_id, target_id);
  }

  void AddPendingReference(std::string_view field_name,
                           std::optional<uint64_t> field_class_id,
                           uint64_t target_id) {
    pending_references_.emplace_back(field_name, field_class_id, target_id);
  }

  const std::vector<Reference>& GetReferences() const { return references_; }

  const std::vector<Reference>& GetPendingReferences() const {
    return pending_references_;
  }

  // Array-specific data
  void SetArrayElements(std::vector<uint64_t> elements) {
    array_elements_ = std::move(elements);
  }

  void SetArrayElementType(FieldType type) { array_element_type_ = type; }

  const std::vector<uint64_t>& GetArrayElements() const {
    return array_elements_;
  }

  FieldType GetArrayElementType() const { return array_element_type_; }

  void AddField(Field field) { fields_.push_back(std::move(field)); }

  const std::vector<Field>& GetFields() const { return fields_; }

  const Field* FindField(const std::string& name) const {
    for (const auto& field : fields_) {
      if (field.GetName() == name) {
        return &field;
      }
    }
    return nullptr;
  }

  int64_t GetNativeSize() const { return native_size_; }

  void AddNativeSize(int64_t size) { native_size_ += size; }

  void SetSelfSizeOverride(size_t size) { self_size_override_ = size; }
  std::optional<size_t> GetSelfSizeOverride() const {
    return self_size_override_;
  }

  template <typename T>
  void SetArrayData(std::vector<T> data) {
    array_data_ = std::move(data);
  }

  bool HasArrayData() const {
    return !std::holds_alternative<std::monostate>(array_data_);
  }

  template <typename T>
  std::vector<T> GetArrayData() const {
    const auto* data = std::get_if<std::vector<T>>(&array_data_);
    if (data) {
      return *data;
    }
    return {};
  }

  size_t GetArrayElementCount() const {
    return std::visit(
        [](const auto& val) -> size_t {
          if constexpr (std::is_same_v<std::decay_t<decltype(val)>,
                                       std::monostate>)
            return 0;
          else
            return val.size();
        },
        array_data_);
  }

 private:
  uint64_t id_ = 0;
  uint64_t class_id_ = 0;
  ObjectType type_ = ObjectType::kInstance;
  bool is_root_ = false;
  bool is_reachable_ = false;
  int32_t root_distance_ = -1;
  std::optional<HprofHeapRootTag> root_type_;
  std::string heap_type_;

  // Data storage - used differently based on object type
  std::vector<uint8_t> raw_data_;
  std::vector<Reference> references_;
  std::vector<Reference> pending_references_;
  std::vector<uint64_t> array_elements_;
  FieldType array_element_type_ = FieldType::kObject;

  int64_t native_size_ = 0;
  std::optional<size_t> self_size_override_;

  // Field values
  std::vector<Field> fields_;
  ArrayData array_data_;
};

}  // namespace perfetto::trace_processor::art_hprof

#endif  // SRC_TRACE_PROCESSOR_IMPORTERS_ART_HPROF_ART_HPROF_MODEL_H_
