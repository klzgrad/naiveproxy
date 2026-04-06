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

#include "src/trace_processor/util/json_value.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

#include "perfetto/base/status.h"
#include "perfetto/ext/base/no_destructor.h"
#include "perfetto/ext/base/status_macros.h"
#include "perfetto/ext/base/status_or.h"
#include "src/trace_processor/util/json_parser.h"
#include "src/trace_processor/util/json_serializer.h"

namespace perfetto::trace_processor::json {

const Dom& NullDom() {
  static base::NoDestructor<Dom> kNull;
  return kNull.ref();
}

// Type constructor.
Dom::Dom(Type type) {
  switch (type) {
    case Type::kNull:
      data_ = std::monostate{};
      break;
    case Type::kBool:
      data_ = false;
      break;
    case Type::kInt:
      data_ = int64_t{0};
      break;
    case Type::kUint:
      data_ = uint64_t{0};
      break;
    case Type::kReal:
      data_ = 0.0;
      break;
    case Type::kString:
      data_ = std::string{};
      break;
    case Type::kArray:
      data_ = Array{};
      break;
    case Type::kObject:
      data_ = Object{};
      break;
  }
}

Type Dom::type() const {
  if (std::holds_alternative<std::monostate>(data_))
    return Type::kNull;
  if (std::holds_alternative<bool>(data_))
    return Type::kBool;
  if (std::holds_alternative<int64_t>(data_))
    return Type::kInt;
  if (std::holds_alternative<uint64_t>(data_))
    return Type::kUint;
  if (std::holds_alternative<double>(data_))
    return Type::kReal;
  if (std::holds_alternative<std::string>(data_))
    return Type::kString;
  if (std::holds_alternative<Array>(data_))
    return Type::kArray;
  return Type::kObject;
}

bool Dom::AsBool() const {
  if (const auto* v = std::get_if<bool>(&data_))
    return *v;
  if (const auto* v = std::get_if<int64_t>(&data_))
    return *v != 0;
  if (const auto* v = std::get_if<uint64_t>(&data_))
    return *v != 0;
  return false;
}

int Dom::AsInt() const {
  return static_cast<int>(AsInt64());
}

int64_t Dom::AsInt64() const {
  if (const auto* v = std::get_if<int64_t>(&data_))
    return *v;
  if (const auto* v = std::get_if<uint64_t>(&data_))
    return static_cast<int64_t>(*v);
  if (const auto* v = std::get_if<double>(&data_))
    return static_cast<int64_t>(*v);
  if (const auto* v = std::get_if<bool>(&data_))
    return *v ? 1 : 0;
  return 0;
}

uint64_t Dom::AsUint() const {
  return AsUint64();
}

uint64_t Dom::AsUint64() const {
  if (const auto* v = std::get_if<uint64_t>(&data_))
    return *v;
  if (const auto* v = std::get_if<int64_t>(&data_))
    return static_cast<uint64_t>(*v);
  if (const auto* v = std::get_if<double>(&data_))
    return static_cast<uint64_t>(*v);
  if (const auto* v = std::get_if<bool>(&data_))
    return *v ? 1 : 0;
  return 0;
}

double Dom::AsDouble() const {
  if (const auto* v = std::get_if<double>(&data_))
    return *v;
  if (const auto* v = std::get_if<int64_t>(&data_))
    return static_cast<double>(*v);
  if (const auto* v = std::get_if<uint64_t>(&data_))
    return static_cast<double>(*v);
  if (const auto* v = std::get_if<bool>(&data_))
    return *v ? 1.0 : 0.0;
  return 0.0;
}

std::string Dom::AsString() const {
  if (const auto* v = std::get_if<std::string>(&data_))
    return *v;
  return std::string{};
}

const char* Dom::AsCString() const {
  if (const auto* v = std::get_if<std::string>(&data_))
    return v->c_str();
  return "";
}

Dom& Dom::operator[](const char* key) {
  return (*this)[std::string(key)];
}

Dom& Dom::operator[](const std::string& key) {
  if (!IsObject()) {
    data_ = Object{};
  }
  auto& obj = std::get<Object>(data_);
  auto it = obj.find(key);
  if (it == obj.end()) {
    obj.insert({key, Dom{}});
    it = obj.find(key);
  }
  return it->second;
}

const Dom& Dom::operator[](const char* key) const {
  return (*this)[std::string(key)];
}

const Dom& Dom::operator[](const std::string& key) const {
  if (const auto* obj = std::get_if<Object>(&data_)) {
    auto it = obj->find(key);
    return it != obj->end() ? it->second : NullDom();
  }
  return NullDom();
}

bool Dom::HasMember(const char* key) const {
  return HasMember(std::string(key));
}

bool Dom::HasMember(const std::string& key) const {
  if (const auto* obj = std::get_if<Object>(&data_)) {
    return obj->find(key) != obj->end();
  }
  return false;
}

std::vector<std::string> Dom::GetMemberNames() const {
  std::vector<std::string> names;
  if (const auto* obj = std::get_if<Object>(&data_)) {
    for (const auto& it : *obj) {
      names.push_back(it.first);
    }
  }
  return names;
}

void Dom::RemoveMember(const char* key) {
  RemoveMember(std::string(key));
}

void Dom::RemoveMember(const std::string& key) {
  if (auto* obj = std::get_if<Object>(&data_)) {
    obj->erase(key);
  }
}

Dom& Dom::operator[](size_t index) {
  if (!IsArray()) {
    data_ = Array{};
  }
  auto& arr = std::get<Array>(data_);
  if (index >= arr.size()) {
    arr.resize(index + 1);
  }
  return arr[index];
}

const Dom& Dom::operator[](size_t index) const {
  if (const auto* arr = std::get_if<Array>(&data_)) {
    if (index < arr->size()) {
      return (*arr)[index];
    }
  }
  return NullDom();
}

Dom& Dom::operator[](int index) {
  return (*this)[static_cast<size_t>(index)];
}

const Dom& Dom::operator[](int index) const {
  return (*this)[static_cast<size_t>(index)];
}

void Dom::Append(Dom&& value) {
  if (!IsArray()) {
    data_ = Array{};
  }
  std::get<Array>(data_).push_back(std::move(value));
}

size_t Dom::size() const {
  if (const auto* arr = std::get_if<Array>(&data_))
    return arr->size();
  if (const auto* obj = std::get_if<Object>(&data_))
    return obj->size();
  return 0;
}

bool Dom::empty() const {
  if (IsNull())
    return true;
  if (const auto* arr = std::get_if<Array>(&data_))
    return arr->empty();
  if (const auto* obj = std::get_if<Object>(&data_))
    return obj->size() == 0;
  if (const auto* str = std::get_if<std::string>(&data_))
    return str->empty();
  return false;
}

void Dom::Clear() {
  if (auto* arr = std::get_if<Array>(&data_))
    arr->clear();
  else if (auto* obj = std::get_if<Object>(&data_))
    obj->clear();
}

Dom::ArrayIterator Dom::begin() const {
  if (const auto* arr = std::get_if<Array>(&data_))
    return ArrayIterator(arr->begin());
  return {};
}

Dom::ArrayIterator Dom::end() const {
  if (const auto* arr = std::get_if<Array>(&data_))
    return ArrayIterator(arr->end());
  return {};
}

Dom Dom::Copy() const {
  switch (type()) {
    case Type::kNull:
      return Dom{};
    case Type::kBool:
      return Dom{AsBool()};
    case Type::kInt:
      return Dom{AsInt64()};
    case Type::kUint:
      return Dom{AsUint64()};
    case Type::kReal:
      return Dom{AsDouble()};
    case Type::kString:
      return Dom{AsString()};
    case Type::kArray: {
      Dom result(Type::kArray);
      if (const auto* arr = GetArray()) {
        for (const auto& elem : *arr) {
          result.Append(elem.Copy());
        }
      }
      return result;
    }
    case Type::kObject: {
      Dom result(Type::kObject);
      if (const auto* obj = GetObject()) {
        for (const auto& it : *obj) {
          result[it.first] = it.second.Copy();
        }
      }
      return result;
    }
  }
  return Dom{};
}

namespace {

void SerializeValue(const Dom& value, JsonSerializer& s) {
  switch (value.type()) {
    case Type::kNull:
      s.NullValue();
      break;
    case Type::kBool:
      s.BoolValue(value.AsBool());
      break;
    case Type::kInt:
      s.NumberValue(value.AsInt64());
      break;
    case Type::kUint:
      s.NumberValue(value.AsUint64());
      break;
    case Type::kReal:
      s.DoubleValue(value.AsDouble());
      break;
    case Type::kString:
      s.StringValue(value.AsString());
      break;
    case Type::kArray:
      s.OpenArray();
      if (const auto* arr = value.GetArray()) {
        for (const auto& elem : *arr) {
          SerializeValue(elem, s);
        }
      }
      s.CloseArray();
      break;
    case Type::kObject:
      s.OpenObject();
      if (const auto* obj = value.GetObject()) {
        for (const auto& it : *obj) {
          s.Key(it.first);
          SerializeValue(it.second, s);
        }
      }
      s.CloseObject();
      break;
  }
}

// Recursively parses JSON using the Iterator and builds a Dom.
base::StatusOr<Dom> ParseRecursive(Iterator& iter) {
  const auto& val = iter.value();

  // Handle primitive types.
  if (std::holds_alternative<Null>(val)) {
    return Dom{};
  }
  if (const auto* b = std::get_if<bool>(&val)) {
    return Dom{*b};
  }
  if (const auto* i = std::get_if<int64_t>(&val)) {
    return Dom{*i};
  }
  if (const auto* d = std::get_if<double>(&val)) {
    return Dom{*d};
  }
  if (const auto* sv = std::get_if<std::string_view>(&val)) {
    return Dom{std::string(*sv)};
  }

  // Handle objects.
  if (std::holds_alternative<Object>(val)) {
    Dom result(Type::kObject);
    while (true) {
      auto rc = iter.ParseAndRecurse();
      if (rc == ReturnCode::kEndOfScope) {
        break;
      }
      if (rc != ReturnCode::kOk) {
        return base::ErrStatus("Failed to parse object: %s",
                               iter.status().c_message());
      }
      std::string key(iter.key());
      ASSIGN_OR_RETURN(Dom child, ParseRecursive(iter));
      result[key] = std::move(child);
    }
    return std::move(result);
  }

  // Handle arrays.
  if (std::holds_alternative<Array>(val)) {
    Dom result(Type::kArray);
    while (true) {
      auto rc = iter.ParseAndRecurse();
      if (rc == ReturnCode::kEndOfScope) {
        break;
      }
      if (rc != ReturnCode::kOk) {
        return base::ErrStatus("Failed to parse array: %s",
                               iter.status().c_message());
      }
      ASSIGN_OR_RETURN(Dom child, ParseRecursive(iter));
      result.Append(std::move(child));
    }
    return std::move(result);
  }

  return base::ErrStatus("Unknown JSON value type");
}

}  // namespace

std::string Serialize(const Dom& value) {
  JsonSerializer s;
  SerializeValue(value, s);
  return s.ToString();
}

base::StatusOr<Dom> Parse(std::string_view json) {
  if (json.empty()) {
    return base::ErrStatus("Empty JSON input");
  }

  Iterator iter;
  iter.Reset(json.data(), json.data() + json.size());

  if (!iter.ParseStart()) {
    return base::ErrStatus("Failed to start parsing: %s",
                           iter.status().c_message());
  }

  // Determine if we're parsing an object or array at the root.
  const auto& parse_stack = iter.parse_stack();
  if (parse_stack.empty()) {
    return base::ErrStatus("Empty parse stack after ParseStart");
  }

  Dom result;
  if (parse_stack.back() == Iterator::ParseType::kObject) {
    result = Dom(Type::kObject);
    while (true) {
      auto rc = iter.ParseAndRecurse();
      if (rc == ReturnCode::kEndOfScope) {
        break;
      }
      if (rc != ReturnCode::kOk) {
        return base::ErrStatus("Failed to parse object: %s",
                               iter.status().c_message());
      }
      std::string key(iter.key());
      ASSIGN_OR_RETURN(Dom child, ParseRecursive(iter));
      result[key] = std::move(child);
    }
  } else {
    result = Dom(Type::kArray);
    while (true) {
      auto rc = iter.ParseAndRecurse();
      if (rc == ReturnCode::kEndOfScope) {
        break;
      }
      if (rc != ReturnCode::kOk) {
        return base::ErrStatus("Failed to parse array: %s",
                               iter.status().c_message());
      }
      ASSIGN_OR_RETURN(Dom child, ParseRecursive(iter));
      result.Append(std::move(child));
    }
  }

  return std::move(result);
}

}  // namespace perfetto::trace_processor::json
