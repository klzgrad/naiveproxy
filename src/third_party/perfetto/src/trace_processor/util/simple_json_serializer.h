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

#ifndef SRC_TRACE_PROCESSOR_UTIL_SIMPLE_JSON_SERIALIZER_H_
#define SRC_TRACE_PROCESSOR_UTIL_SIMPLE_JSON_SERIALIZER_H_

#include <cstdint>
#include <functional>
#include <string>
#include <string_view>

#include "src/trace_processor/util/json_serializer.h"

namespace perfetto::trace_processor::json {

class JsonDictSerializer;
class JsonArraySerializer;
class JsonValueSerializer;

// Main entry point for serializing JSON using a callback-based API.
// This is a higher-level wrapper around JsonSerializer that provides
// a more ergonomic callback-based interface.
//
// Usage:
//   std::string json = SerializeJson([](JsonValueSerializer&& writer) {
//     std::move(writer).WriteDict([](JsonDictSerializer& dict) {
//       dict.AddString("hello", "world");
//     });
//   });
std::string SerializeJson(
    std::function<void(JsonValueSerializer&&)> value_writer);

// Serializes a JSON dictionary.
// Usage example:
//   dict.AddString("key", "value");
//   dict.AddDict("nested", [](JsonDictSerializer& nested) {
//     nested.AddInt("count", 42);
//   });
class JsonDictSerializer {
 public:
  explicit JsonDictSerializer(JsonSerializer& serializer);
  JsonDictSerializer(const JsonDictSerializer&) = delete;
  JsonDictSerializer& operator=(const JsonDictSerializer&) = delete;

  // Primitive values.
  void AddNull(std::string_view key);
  void AddBool(std::string_view key, bool value);
  void AddInt(std::string_view key, int64_t value);
  void AddUint(std::string_view key, uint64_t value);
  void AddDouble(std::string_view key, double value);
  void AddString(std::string_view key, std::string_view value);

  // Writes a nested dictionary.
  void AddDict(std::string_view key,
               std::function<void(JsonDictSerializer&)> dict_writer);

  // Writes a nested array.
  void AddArray(std::string_view key,
                std::function<void(JsonArraySerializer&)> array_writer);

  // Writes a generic value.
  void Add(std::string_view key,
           std::function<void(JsonValueSerializer&&)> value_writer);

 private:
  JsonSerializer& serializer_;
};

// Serializes a JSON array.
// Usage example:
//   array.AppendString("item1");
//   array.AppendDict([](JsonDictSerializer& dict) {
//     dict.AddString("key", "value");
//   });
class JsonArraySerializer {
 public:
  explicit JsonArraySerializer(JsonSerializer& serializer);
  JsonArraySerializer(const JsonArraySerializer&) = delete;
  JsonArraySerializer& operator=(const JsonArraySerializer&) = delete;

  // Primitive values.
  void AppendNull();
  void AppendBool(bool value);
  void AppendInt(int64_t value);
  void AppendUint(uint64_t value);
  void AppendDouble(double value);
  void AppendString(std::string_view value);

  // Writes a nested dictionary.
  void AppendDict(std::function<void(JsonDictSerializer&)> dict_writer);

  // Writes a nested array.
  void AppendArray(std::function<void(JsonArraySerializer&)> array_writer);

  // Writes a generic value.
  void Append(std::function<void(JsonValueSerializer&&)> value_writer);

 private:
  JsonSerializer& serializer_;
};

// Generic value serializer.
// Usage example:
//   [](JsonValueSerializer&& writer) {
//     std::move(writer).WriteString("foo");
//   });
class JsonValueSerializer {
 public:
  explicit JsonValueSerializer(JsonSerializer& serializer);
  JsonValueSerializer(const JsonValueSerializer&) = delete;
  JsonValueSerializer& operator=(const JsonValueSerializer&) = delete;

  void WriteNull() &&;
  void WriteBool(bool value) &&;
  void WriteInt(int64_t value) &&;
  void WriteUint(uint64_t value) &&;
  void WriteDouble(double value) &&;
  void WriteString(std::string_view value) &&;
  void WriteDict(std::function<void(JsonDictSerializer&)> dict_writer) &&;
  void WriteArray(std::function<void(JsonArraySerializer&)> array_writer) &&;

 private:
  JsonSerializer& serializer_;
};

}  // namespace perfetto::trace_processor::json

#endif  // SRC_TRACE_PROCESSOR_UTIL_SIMPLE_JSON_SERIALIZER_H_
