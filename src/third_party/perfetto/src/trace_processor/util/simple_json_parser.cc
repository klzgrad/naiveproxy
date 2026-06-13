/*
 * Copyright (C) 2024 The Android Open Source Project
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

#include "src/trace_processor/util/simple_json_parser.h"

#include <cstdint>
#include <string>
#include <vector>

#include "perfetto/ext/base/status_or.h"

namespace perfetto::trace_processor::json {

base::StatusOr<std::vector<uint32_t>> SimpleJsonParser::CollectUint32Array() {
  std::vector<uint32_t> result;
  RETURN_IF_ERROR(ForEachArrayElement([&]() {
    if (auto v = GetUint32()) {
      result.push_back(*v);
    }
    return base::OkStatus();
  }));
  return result;
}

base::StatusOr<std::vector<int64_t>> SimpleJsonParser::CollectInt64Array() {
  std::vector<int64_t> result;
  RETURN_IF_ERROR(ForEachArrayElement([&]() {
    if (auto v = GetInt64()) {
      result.push_back(*v);
    }
    return base::OkStatus();
  }));
  return result;
}

base::StatusOr<std::vector<std::string>>
SimpleJsonParser::CollectStringArray() {
  std::vector<std::string> result;
  RETURN_IF_ERROR(ForEachArrayElement([&]() {
    if (auto v = GetString()) {
      result.push_back(std::string(*v));
    }
    return base::OkStatus();
  }));
  return result;
}

base::StatusOr<std::vector<double>> SimpleJsonParser::CollectDoubleArray() {
  std::vector<double> result;
  RETURN_IF_ERROR(ForEachArrayElement([&]() {
    if (auto v = GetDouble()) {
      result.push_back(*v);
    }
    return base::OkStatus();
  }));
  return result;
}

}  // namespace perfetto::trace_processor::json
