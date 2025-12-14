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

#ifndef SRC_TRACE_PROCESSOR_UTIL_JSON_WRITER_H_
#define SRC_TRACE_PROCESSOR_UTIL_JSON_WRITER_H_

#include <cstdint>
#include <functional>
#include <string>
#include <string_view>

#include "perfetto/ext/base/dynamic_string_writer.h"

namespace perfetto::trace_processor::json {

class JsonDictWriter;
class JsonArrayWriter;
class JsonValueWriter;

// Main entry point for writing JSON.
// Usage:
//   std::string json = write([](JsonValueWriter writer) {
//     std::move(writer).WriteDict([](JsonDictWriter dict) {
//       dict.AddString("hello", "world");
//     });
//   });
std::string Write(std::function<void(JsonValueWriter&&)> value_writer);

// Writes a JSON dictionary.
// Usage example:
//   dict.AddString("key", "value");
//   dict.AddDict("nested", [](JsonDictWriter& nested) {
//     nested.AddInt("count", 42);
//   });
class JsonDictWriter {
 public:
  explicit JsonDictWriter(base::DynamicStringWriter& writer);
  JsonDictWriter(const JsonDictWriter&) = delete;
  JsonDictWriter& operator=(const JsonDictWriter&) = delete;

  // Primitive values.
  void AddNull(std::string_view key);
  void AddBool(std::string_view key, bool value);
  void AddInt(std::string_view key, int64_t value);
  void AddUint(std::string_view key, uint64_t value);
  void AddDouble(std::string_view key, double value);
  void AddString(std::string_view key, std::string_view value);

  // Writes a nested dictionary.
  void AddDict(std::string_view key,
               std::function<void(JsonDictWriter&)> dict_writer);

  // Writes a nested array.
  void AddArray(std::string_view key,
                std::function<void(JsonArrayWriter&)> array_writer);

  // Writes a generic value.
  void Add(std::string_view key,
           std::function<void(JsonValueWriter&&)> value_writer);

 private:
  void WriteKey(std::string_view key);

  base::DynamicStringWriter& buffer_;
  bool first_;
};

// Writes a JSON array.
// Usage example:
//   array.AppendString("item1");
//   array.AppendDict([](JsonDictWriter& dict) {
//     dict.AddString("key", "value");
//   });
class JsonArrayWriter {
 public:
  explicit JsonArrayWriter(base::DynamicStringWriter& writer);
  JsonArrayWriter(const JsonArrayWriter&) = delete;
  JsonArrayWriter& operator=(const JsonArrayWriter&) = delete;

  // Primitive values.
  void AppendNull();
  void AppendBool(bool value);
  void AppendInt(int64_t value);
  void AppendUint(uint64_t value);
  void AppendDouble(double value);
  void AppendString(std::string_view value);

  // Writes a nested dictionary.
  void AppendDict(std::function<void(JsonDictWriter&)> dict_writer);

  // Writes a nested array.
  void AppendArray(std::function<void(JsonArrayWriter&)> array_writer);

  // Writes a generic value.
  void Append(std::function<void(JsonValueWriter&&)> value_writer);

 private:
  void AddSeparator();

  base::DynamicStringWriter& buffer_;
  bool first_;
};

// Generic value writer.
// Usage example:
//   [](JsonValueWriter writer) {
//     std::move(writer).WriteString("foo");
//   });
class JsonValueWriter {
 public:
  explicit JsonValueWriter(base::DynamicStringWriter& writer);
  JsonValueWriter(const JsonValueWriter&) = delete;
  JsonValueWriter& operator=(const JsonValueWriter&) = delete;

  void WriteNull() &&;
  void WriteBool(bool value) &&;
  void WriteInt(int64_t value) &&;
  void WriteUint(uint64_t value) &&;
  void WriteDouble(double value) &&;
  void WriteString(std::string_view value) &&;
  void WriteDict(std::function<void(JsonDictWriter&)> dict_writer) &&;
  void WriteArray(std::function<void(JsonArrayWriter&)> array_writer) &&;

 private:
  void WriteEscapedString(std::string_view value);

  base::DynamicStringWriter& buffer_;
};

}  // namespace perfetto::trace_processor::json

#endif  // SRC_TRACE_PROCESSOR_UTIL_JSON_WRITER_H_
