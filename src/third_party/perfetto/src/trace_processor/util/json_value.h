/*
 * Copyright (C) 2025 The Android Open Source Project
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

#ifndef SRC_TRACE_PROCESSOR_UTIL_JSON_VALUE_H_
#define SRC_TRACE_PROCESSOR_UTIL_JSON_VALUE_H_

#include <cstddef>
#include <cstdint>
#include <iterator>
#include <map>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

#include "perfetto/ext/base/status_or.h"

namespace perfetto::trace_processor::json {

// Type of JSON value.
enum class Type {
  kNull,
  kBool,
  kInt,
  kUint,
  kReal,
  kString,
  kArray,
  kObject,
};

// Forward declaration.
class Dom;

// Returns a reference to a static null Dom value.
const Dom& NullDom();

// DOM-based JSON value class.
// Provides a jsoncpp-like API for reading and writing JSON values.
class Dom {
 public:
  using Object = std::map<std::string, Dom>;
  using Array = std::vector<Dom>;

  // Default constructor creates null value.
  Dom() : data_(std::monostate{}) {}

  // Type constructor.
  explicit Dom(Type type);

  // Value constructors.
  Dom(bool v) : data_(v) {}
  Dom(int v) : data_(static_cast<int64_t>(v)) {}
  Dom(int64_t v) : data_(v) {}
  Dom(uint64_t v) : data_(v) {}
  Dom(double v) : data_(v) {}
  Dom(const char* v) : data_(std::string(v)) {}
  Dom(std::string v) : data_(std::move(v)) {}
  Dom(std::string_view v) : data_(std::string(v)) {}

  // Move only - FlatHashMap doesn't support copy.
  Dom(const Dom& other) = delete;
  Dom& operator=(const Dom& other) = delete;
  Dom(Dom&& other) noexcept = default;
  Dom& operator=(Dom&& other) noexcept = default;

  // Type queries.
  Type type() const;
  bool IsNull() const { return std::holds_alternative<std::monostate>(data_); }
  bool IsBool() const { return std::holds_alternative<bool>(data_); }
  bool IsInt() const { return std::holds_alternative<int64_t>(data_); }
  bool IsUint() const { return std::holds_alternative<uint64_t>(data_); }
  bool IsDouble() const { return std::holds_alternative<double>(data_); }
  bool IsNumeric() const { return IsInt() || IsUint() || IsDouble(); }
  bool IsString() const { return std::holds_alternative<std::string>(data_); }
  bool IsArray() const { return std::holds_alternative<Array>(data_); }
  bool IsObject() const { return std::holds_alternative<Object>(data_); }

  // Value accessors with type coercion.
  bool AsBool() const;
  int AsInt() const;
  int64_t AsInt64() const;
  uint64_t AsUint() const;
  uint64_t AsUint64() const;
  double AsDouble() const;
  std::string AsString() const;
  const char* AsCString() const;

  // Object operations.
  Dom& operator[](const char* key);
  Dom& operator[](const std::string& key);
  const Dom& operator[](const char* key) const;
  const Dom& operator[](const std::string& key) const;
  bool HasMember(const char* key) const;
  bool HasMember(const std::string& key) const;
  std::vector<std::string> GetMemberNames() const;
  void RemoveMember(const char* key);
  void RemoveMember(const std::string& key);

  // Array operations.
  Dom& operator[](size_t index);
  const Dom& operator[](size_t index) const;
  Dom& operator[](int index);
  const Dom& operator[](int index) const;
  void Append(Dom&& value);

  // Size/empty.
  size_t size() const;
  bool empty() const;
  void Clear();

  // Array iteration.
  class ArrayIterator {
   public:
    using iterator_category = std::forward_iterator_tag;
    using value_type = Dom;
    using difference_type = std::ptrdiff_t;
    using pointer = const Dom*;
    using reference = const Dom&;

    ArrayIterator() = default;
    explicit ArrayIterator(Array::const_iterator it) : it_(it) {}

    reference operator*() const { return *it_; }
    pointer operator->() const { return &(*it_); }
    ArrayIterator& operator++() {
      ++it_;
      return *this;
    }
    ArrayIterator operator++(int) {
      ArrayIterator tmp = *this;
      ++it_;
      return tmp;
    }
    bool operator==(const ArrayIterator& other) const {
      return it_ == other.it_;
    }
    bool operator!=(const ArrayIterator& other) const {
      return it_ != other.it_;
    }

   private:
    Array::const_iterator it_;
  };

  ArrayIterator begin() const;
  ArrayIterator end() const;

  // Access underlying data for serialization.
  const Array* GetArray() const { return std::get_if<Array>(&data_); }
  const Object* GetObject() const { return std::get_if<Object>(&data_); }
  Array* GetMutableArray() { return std::get_if<Array>(&data_); }
  Object* GetMutableObject() { return std::get_if<Object>(&data_); }

  // Deep copy.
  Dom Copy() const;

 private:
  std::variant<std::monostate,  // null
               bool,
               int64_t,
               uint64_t,
               double,
               std::string,
               Array,
               Object>
      data_;
};

// Serializes a Dom value to a JSON string.
std::string Serialize(const Dom& value);

// Parses a JSON string into a Dom value.
// Returns an error status if parsing fails.
base::StatusOr<Dom> Parse(std::string_view json);

}  // namespace perfetto::trace_processor::json

#endif  // SRC_TRACE_PROCESSOR_UTIL_JSON_VALUE_H_
