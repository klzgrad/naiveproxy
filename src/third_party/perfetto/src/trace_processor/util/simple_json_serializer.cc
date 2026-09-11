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

#include "src/trace_processor/util/simple_json_serializer.h"

#include <cstdint>
#include <functional>
#include <string>
#include <string_view>
#include <utility>

#include "src/trace_processor/util/json_serializer.h"

namespace perfetto::trace_processor::json {

// JsonDictSerializer implementation - delegates to JsonSerializer

JsonDictSerializer::JsonDictSerializer(JsonSerializer& serializer)
    : serializer_(serializer) {}

void JsonDictSerializer::AddNull(std::string_view key) {
  serializer_.Key(key);
  serializer_.NullValue();
}

void JsonDictSerializer::AddBool(std::string_view key, bool value) {
  serializer_.Key(key);
  serializer_.BoolValue(value);
}

void JsonDictSerializer::AddInt(std::string_view key, int64_t value) {
  serializer_.Key(key);
  serializer_.NumberValue(value);
}

void JsonDictSerializer::AddUint(std::string_view key, uint64_t value) {
  serializer_.Key(key);
  serializer_.NumberValue(value);
}

void JsonDictSerializer::AddDouble(std::string_view key, double value) {
  serializer_.Key(key);
  serializer_.DoubleValue(value);
}

void JsonDictSerializer::AddString(std::string_view key,
                                   std::string_view value) {
  serializer_.Key(key);
  serializer_.StringValue(value);
}

void JsonDictSerializer::AddDict(
    std::string_view key,
    std::function<void(JsonDictSerializer&)> dict_writer) {
  serializer_.Key(key);
  serializer_.OpenObject();
  JsonDictSerializer nested(serializer_);
  dict_writer(nested);
  serializer_.CloseObject();
}

void JsonDictSerializer::AddArray(
    std::string_view key,
    std::function<void(JsonArraySerializer&)> array_writer) {
  serializer_.Key(key);
  serializer_.OpenArray();
  JsonArraySerializer nested(serializer_);
  array_writer(nested);
  serializer_.CloseArray();
}

void JsonDictSerializer::Add(
    std::string_view key,
    std::function<void(JsonValueSerializer&&)> value_writer) {
  serializer_.Key(key);
  value_writer(JsonValueSerializer(serializer_));
}

// JsonArraySerializer implementation - delegates to JsonSerializer

JsonArraySerializer::JsonArraySerializer(JsonSerializer& serializer)
    : serializer_(serializer) {}

void JsonArraySerializer::AppendNull() {
  serializer_.NullValue();
}

void JsonArraySerializer::AppendBool(bool value) {
  serializer_.BoolValue(value);
}

void JsonArraySerializer::AppendInt(int64_t value) {
  serializer_.NumberValue(value);
}

void JsonArraySerializer::AppendUint(uint64_t value) {
  serializer_.NumberValue(value);
}

void JsonArraySerializer::AppendDouble(double value) {
  serializer_.DoubleValue(value);
}

void JsonArraySerializer::AppendString(std::string_view value) {
  serializer_.StringValue(value);
}

void JsonArraySerializer::AppendDict(
    std::function<void(JsonDictSerializer&)> dict_writer) {
  serializer_.OpenObject();
  JsonDictSerializer nested(serializer_);
  dict_writer(nested);
  serializer_.CloseObject();
}

void JsonArraySerializer::AppendArray(
    std::function<void(JsonArraySerializer&)> array_writer) {
  serializer_.OpenArray();
  JsonArraySerializer nested(serializer_);
  array_writer(nested);
  serializer_.CloseArray();
}

void JsonArraySerializer::Append(
    std::function<void(JsonValueSerializer&&)> value_writer) {
  value_writer(JsonValueSerializer(serializer_));
}

// JsonValueSerializer implementation - delegates to JsonSerializer

JsonValueSerializer::JsonValueSerializer(JsonSerializer& serializer)
    : serializer_(serializer) {}

void JsonValueSerializer::WriteNull() && {
  serializer_.NullValue();
}

void JsonValueSerializer::WriteBool(bool value) && {
  serializer_.BoolValue(value);
}

void JsonValueSerializer::WriteInt(int64_t value) && {
  serializer_.NumberValue(value);
}

void JsonValueSerializer::WriteUint(uint64_t value) && {
  serializer_.NumberValue(value);
}

void JsonValueSerializer::WriteDouble(double value) && {
  serializer_.DoubleValue(value);
}

void JsonValueSerializer::WriteString(std::string_view value) && {
  serializer_.StringValue(value);
}

void JsonValueSerializer::WriteDict(
    std::function<void(JsonDictSerializer&)> dict_writer) && {
  serializer_.OpenObject();
  JsonDictSerializer nested(serializer_);
  dict_writer(nested);
  serializer_.CloseObject();
}

void JsonValueSerializer::WriteArray(
    std::function<void(JsonArraySerializer&)> array_writer) && {
  serializer_.OpenArray();
  JsonArraySerializer nested(serializer_);
  array_writer(nested);
  serializer_.CloseArray();
}

// Main entry point

std::string SerializeJson(
    std::function<void(JsonValueSerializer&&)> value_writer) {
  JsonSerializer serializer;
  value_writer(JsonValueSerializer(serializer));
  return serializer.ToString();
}

}  // namespace perfetto::trace_processor::json
