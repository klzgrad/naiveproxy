/*
 * Copyright (C) 2021 The Android Open Source Project
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

#include "perfetto/test/traced_value_test_support.h"

#include "protos/perfetto/trace/track_event/debug_annotation.gen.h"
#include "protos/perfetto/trace/track_event/debug_annotation.pb.h"

#include <sstream>

namespace perfetto {

namespace internal {

namespace {

void WriteAsJSON(const protos::DebugAnnotation& value, std::stringstream& ss) {
  if (value.has_bool_value()) {
    if (value.bool_value()) {
      ss << "true";
    } else {
      ss << "false";
    }
  } else if (value.has_uint_value()) {
    ss << value.uint_value();
  } else if (value.has_int_value()) {
    ss << value.int_value();
  } else if (value.has_double_value()) {
    ss << value.double_value();
  } else if (value.has_string_value()) {
    ss << value.string_value();
  } else if (value.has_pointer_value()) {
    // Printing pointer values via ostream is really platform-specific, so do
    // not try to convert it to void* before printing.
    ss << "0x" << std::hex << value.pointer_value() << std::dec;
  } else if (value.dict_entries_size() > 0) {
    ss << "{";
    for (int i = 0; i < value.dict_entries_size(); ++i) {
      if (i > 0)
        ss << ",";
      auto item = value.dict_entries(i);
      ss << item.name();
      ss << ":";
      WriteAsJSON(item, ss);
    }
    ss << "}";
  } else if (value.array_values_size() > 0) {
    ss << "[";
    for (int i = 0; i < value.array_values_size(); ++i) {
      auto item = value.array_values(i);
      if (i > 0)
        ss << ",";
      WriteAsJSON(item, ss);
    }
    ss << "]";
  } else {
    ss << "{}";
  }
}

}  // namespace

std::string DebugAnnotationToString(const std::string& data) {
  std::stringstream ss;
  protos::DebugAnnotation result;
  result.ParseFromString(data);
  WriteAsJSON(result, ss);
  return ss.str();
}

}  // namespace internal
}  // namespace perfetto
