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

#include "src/trace_processor/util/json_writer.h"

#include <cmath>
#include <cstdint>
#include <functional>
#include <string>
#include <string_view>
#include <utility>

#include "perfetto/ext/base/dynamic_string_writer.h"
#include "perfetto/ext/base/string_utils.h"

namespace perfetto::trace_processor::json {

namespace {

// Helper function to write an escaped JSON string.
void WriteEscapedJsonString(base::DynamicStringWriter& writer,
                            std::string_view value) {
  writer.AppendChar('"');
  for (char c : value) {
    unsigned char uc = static_cast<unsigned char>(c);
    if (c == '"') {
      writer.AppendLiteral("\\\"");
    } else if (c == '\\') {
      writer.AppendLiteral("\\\\");
    } else if (c == '\n') {
      writer.AppendLiteral("\\n");
    } else if (c == '\r') {
      writer.AppendLiteral("\\r");
    } else if (c == '\t') {
      writer.AppendLiteral("\\t");
    } else if (uc < 0x20) {
      writer.AppendLiteral("\\u00");
      writer.AppendChar("0123456789abcdef"[uc >> 4]);
      writer.AppendChar("0123456789abcdef"[uc & 0xf]);
    } else {
      writer.AppendChar(c);
    }
  }
  writer.AppendChar('"');
}

}  // namespace

JsonDictWriter::JsonDictWriter(base::DynamicStringWriter& writer)
    : buffer_(writer), first_(true) {}

void JsonDictWriter::AddNull(std::string_view key) {
  Add(key, [](JsonValueWriter&& v) { std::move(v).WriteNull(); });
}

void JsonDictWriter::AddBool(std::string_view key, bool value) {
  Add(key, [value](JsonValueWriter&& v) { std::move(v).WriteBool(value); });
}

void JsonDictWriter::AddInt(std::string_view key, int64_t value) {
  Add(key, [value](JsonValueWriter&& v) { std::move(v).WriteInt(value); });
}

void JsonDictWriter::AddUint(std::string_view key, uint64_t value) {
  Add(key, [value](JsonValueWriter&& v) { std::move(v).WriteUint(value); });
}

void JsonDictWriter::AddDouble(std::string_view key, double value) {
  Add(key, [value](JsonValueWriter&& v) { std::move(v).WriteDouble(value); });
}

void JsonDictWriter::AddString(std::string_view key, std::string_view value) {
  Add(key, [value](JsonValueWriter&& v) { std::move(v).WriteString(value); });
}

void JsonDictWriter::WriteKey(std::string_view key) {
  if (!first_) {
    buffer_.AppendChar(',');
  }
  first_ = false;
  WriteEscapedJsonString(buffer_, key);
  buffer_.AppendChar(':');
}

JsonArrayWriter::JsonArrayWriter(base::DynamicStringWriter& writer)
    : buffer_(writer), first_(true) {}

void JsonArrayWriter::AppendNull() {
  Append([](JsonValueWriter&& v) { std::move(v).WriteNull(); });
}

void JsonArrayWriter::AppendBool(bool value) {
  Append([value](JsonValueWriter&& v) { std::move(v).WriteBool(value); });
}

void JsonArrayWriter::AppendInt(int64_t value) {
  Append([value](JsonValueWriter&& v) { std::move(v).WriteInt(value); });
}

void JsonArrayWriter::AppendUint(uint64_t value) {
  Append([value](JsonValueWriter&& v) { std::move(v).WriteUint(value); });
}

void JsonArrayWriter::AppendDouble(double value) {
  Append([value](JsonValueWriter&& v) { std::move(v).WriteDouble(value); });
}

void JsonArrayWriter::AppendString(std::string_view value) {
  Append([value](JsonValueWriter&& v) { std::move(v).WriteString(value); });
}

void JsonArrayWriter::AddSeparator() {
  if (!first_) {
    buffer_.AppendChar(',');
  }
  first_ = false;
}

JsonValueWriter::JsonValueWriter(base::DynamicStringWriter& writer)
    : buffer_(writer) {}

void JsonValueWriter::WriteNull() && {
  buffer_.AppendLiteral("null");
}

void JsonValueWriter::WriteBool(bool value) && {
  buffer_.AppendString(value ? "true" : "false");
}

void JsonValueWriter::WriteInt(int64_t value) && {
  buffer_.AppendInt(value);
}

void JsonValueWriter::WriteUint(uint64_t value) && {
  buffer_.AppendUnsignedInt(value);
}

void JsonValueWriter::WriteDouble(double value) && {
  if (std::isnan(value)) {
    buffer_.AppendLiteral("\"NaN\"");
  } else if (std::isinf(value) && value > 0) {
    buffer_.AppendLiteral("\"Infinity\"");
  } else if (std::isinf(value) && value < 0) {
    buffer_.AppendLiteral("\"-Infinity\"");
  } else {
    buffer_.AppendDouble(value);
  }
}

void JsonValueWriter::WriteString(std::string_view value) && {
  WriteEscapedString(value);
}

void JsonValueWriter::WriteEscapedString(std::string_view value) {
  buffer_.AppendChar('"');
  for (char c : value) {
    unsigned char uc = static_cast<unsigned char>(c);
    if (c == '"') {
      buffer_.AppendLiteral("\\\"");
    } else if (c == '\\') {
      buffer_.AppendLiteral("\\\\");
    } else if (c == '\n') {
      buffer_.AppendLiteral("\\n");
    } else if (c == '\r') {
      buffer_.AppendLiteral("\\r");
    } else if (c == '\t') {
      buffer_.AppendLiteral("\\t");
    } else if (uc < 0x20) {
      buffer_.AppendLiteral("\\u00");
      buffer_.AppendChar("0123456789abcdef"[uc >> 4]);
      buffer_.AppendChar("0123456789abcdef"[uc & 0xf]);
    } else {
      buffer_.AppendChar(c);
    }
  }
  buffer_.AppendChar('"');
}

void JsonDictWriter::Add(std::string_view key,
                         std::function<void(JsonValueWriter&&)> writer) {
  WriteKey(key);
  writer(JsonValueWriter(buffer_));
}

void JsonDictWriter::AddDict(std::string_view key,
                             std::function<void(JsonDictWriter&)> dict_writer) {
  WriteKey(key);
  buffer_.AppendChar('{');
  JsonDictWriter dict(buffer_);
  dict_writer(dict);
  buffer_.AppendChar('}');
}

void JsonDictWriter::AddArray(
    std::string_view key,
    std::function<void(JsonArrayWriter&)> array_writer) {
  WriteKey(key);
  buffer_.AppendChar('[');
  JsonArrayWriter array(buffer_);
  array_writer(array);
  buffer_.AppendChar(']');
}

void JsonArrayWriter::Append(
    std::function<void(JsonValueWriter&&)> value_writer) {
  AddSeparator();
  JsonValueWriter writer(buffer_);
  value_writer(std::move(writer));
}

void JsonArrayWriter::AppendDict(
    std::function<void(JsonDictWriter&)> dict_writer) {
  AddSeparator();
  buffer_.AppendChar('{');
  JsonDictWriter dict(buffer_);
  dict_writer(dict);
  buffer_.AppendChar('}');
}

void JsonArrayWriter::AppendArray(
    std::function<void(JsonArrayWriter&)> array_writer) {
  AddSeparator();
  buffer_.AppendChar('[');
  JsonArrayWriter array(buffer_);
  array_writer(array);
  buffer_.AppendChar(']');
}

void JsonValueWriter::WriteDict(
    std::function<void(JsonDictWriter&)> dict_writer) && {
  buffer_.AppendChar('{');
  JsonDictWriter dict(buffer_);
  dict_writer(dict);
  buffer_.AppendChar('}');
}

void JsonValueWriter::WriteArray(
    std::function<void(JsonArrayWriter&)> array_writer) && {
  buffer_.AppendChar('[');
  JsonArrayWriter array(buffer_);
  array_writer(array);
  buffer_.AppendChar(']');
}

std::string Write(std::function<void(JsonValueWriter&&)> value_writer) {
  base::DynamicStringWriter writer;
  JsonValueWriter json_value_writer(writer);
  value_writer(std::move(json_value_writer));
  return writer.GetStringView().ToStdString();
}

}  // namespace perfetto::trace_processor::json
