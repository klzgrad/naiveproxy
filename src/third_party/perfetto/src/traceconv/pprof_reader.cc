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

#include "src/traceconv/pprof_reader.h"

#include <algorithm>
#include <cinttypes>

#include "perfetto/ext/base/file_utils.h"

namespace perfetto::pprof {

using namespace third_party::perftools::profiles::gen;

PprofProfileReader::PprofProfileReader(const std::string& path) {
  std::string pprof_contents;
  base::ReadFile(path, &pprof_contents);
  profile_.ParseFromString(pprof_contents);
}

uint64_t PprofProfileReader::get_sample_count() const {
  return static_cast<uint64_t>(profile_.sample_size());
}

int64_t PprofProfileReader::get_string_index(const std::string& str) const {
  const auto it = std::find(profile_.string_table().begin(),
                            profile_.string_table().end(), str);

  if (it == profile_.string_table().end()) {
    PERFETTO_FATAL("String %s not found in string table", str.c_str());
  }

  return std::distance(profile_.string_table().begin(), it);
}

std::string PprofProfileReader::get_string_by_index(
    const uint64_t string_index) const {
  if (string_index >= profile_.string_table().size()) {
    PERFETTO_FATAL("String %" PRIu64 " is out of range in string table",
                   string_index);
  }

  return profile_.string_table()[string_index];
}

uint64_t PprofProfileReader::find_location_id(
    const std::string& function_name) const {
  const int64_t function_string_id = get_string_index(function_name);

  // Find a function based on function_name
  uint64_t function_id = 0;
  bool found_function_id = false;

  for (const auto& function : profile_.function()) {
    if (function.name() == function_string_id) {
      function_id = function.id();
      found_function_id = true;
    }
  }

  if (!found_function_id) {
    PERFETTO_FATAL("Function %s not found", function_name.c_str());
  }

  // Find a location for the function
  for (const auto& location : profile_.location()) {
    for (const auto& line : location.line()) {
      if (line.function_id() == function_id) {
        return location.id();
      }
    }
  }

  PERFETTO_FATAL("Location for function %s not found", function_name.c_str());
}

Location PprofProfileReader::find_location(const uint64_t location_id) const {
  const auto it = std::find_if(
      profile_.location().begin(), profile_.location().end(),
      [location_id](const Location& loc) { return loc.id() == location_id; });

  if (it != profile_.location().end()) {
    return *it;
  }

  PERFETTO_FATAL("Location with id %" PRIu64 " not found", location_id);
}

Function PprofProfileReader::find_function(const uint64_t function_id) const {
  const auto it = std::find_if(
      profile_.function().begin(), profile_.function().end(),
      [function_id](const Function& fun) { return fun.id() == function_id; });

  if (it != profile_.function().end()) {
    return *it;
  }

  PERFETTO_FATAL("Function with id %" PRIu64 " not found", function_id);
}

std::vector<std::string> PprofProfileReader::get_sample_function_names(
    const Sample& sample) const {
  std::vector<std::string> function_names;
  for (const auto location_id : sample.location_id()) {
    const auto location = find_location(location_id);

    for (const auto& line : location.line()) {
      Function function = find_function(line.function_id());
      std::string function_name =
          get_string_by_index(static_cast<uint64_t>(function.name()));
      function_names.push_back(function_name);
    }
  }

  return function_names;
}

std::vector<Sample> PprofProfileReader::get_samples(
    const std::string& last_function_name) const {
  const uint64_t location_id = find_location_id(last_function_name);

  std::vector<Sample> samples;
  for (const auto& sample : profile_.sample()) {
    if (sample.location_id_size() == 0) {
      continue;
    }

    // Get the first location id from the iterator as they are stored inverted
    const uint64_t last_location_id = sample.location_id()[0];

    if (last_location_id == location_id) {
      samples.push_back(sample);
    }
  }

  return samples;
}

uint64_t PprofProfileReader::get_sample_value_index(
    const std::string& value_name) const {
  const int64_t value_name_string_index = get_string_index(value_name);

  const auto it =
      std::find_if(profile_.sample_type().begin(), profile_.sample_type().end(),
                   [value_name_string_index](const auto& sample_type) {
                     return sample_type.type() == value_name_string_index;
                   });

  if (it != profile_.sample_type().end()) {
    return static_cast<uint64_t>(
        std::distance(profile_.sample_type().begin(), it));
  }

  PERFETTO_FATAL("Can't find value type with name \"%s\"", value_name.c_str());
}

int64_t PprofProfileReader::get_samples_value_sum(
    const std::string& last_function_name,
    const std::string& value_name) const {
  int64_t total = 0;
  const auto samples = get_samples(last_function_name);
  const auto value_index = get_sample_value_index(value_name);
  for (const auto& sample : samples) {
    total += sample.value()[value_index];
  }
  return total;
}
}  // namespace perfetto::pprof
