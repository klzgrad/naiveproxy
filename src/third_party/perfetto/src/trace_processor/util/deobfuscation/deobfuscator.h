/*
 * Copyright (C) 2019 The Android Open Source Project
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

#ifndef SRC_TRACE_PROCESSOR_UTIL_DEOBFUSCATION_DEOBFUSCATOR_H_
#define SRC_TRACE_PROCESSOR_UTIL_DEOBFUSCATION_DEOBFUSCATOR_H_

#include <cstdint>
#include <functional>
#include <map>
#include <optional>
#include <string>
#include <utility>
#include <vector>
#include "perfetto/base/status.h"

namespace perfetto {
namespace profiling {

std::string FlattenClasses(
    const std::map<std::string, std::vector<std::string>>& m);

// R8 inline method mapping with line number information.
// Multiple entries with the same obfuscated_name and overlapping obfuscated
// line ranges form an inline chain, ordered innermost (inlined) first.
struct MethodMapping {
  std::string obfuscated_name;
  std::string deobfuscated_name;  // Fully qualified: "com.example.Class.method"
  std::optional<uint32_t> obfuscated_line_start;
  std::optional<uint32_t> obfuscated_line_end;
  std::optional<uint32_t> source_line_start;
  std::optional<uint32_t> source_line_end;
};

class ObfuscatedClass {
 public:
  explicit ObfuscatedClass(std::string d) : deobfuscated_name_(std::move(d)) {}

  const std::string& deobfuscated_name() const { return deobfuscated_name_; }

  const std::map<std::string, std::string>& deobfuscated_fields() const {
    return deobfuscated_fields_;
  }

  // Returns map of obfuscated_name -> deobfuscated_name.
  // For R8 inline chains, returns the outermost method.
  // For ambiguous mappings, joins names with " | ".
  std::map<std::string, std::string> deobfuscated_methods() const;

  bool AddField(std::string obfuscated_name, std::string deobfuscated_name) {
    auto p = deobfuscated_fields_.emplace(std::move(obfuscated_name),
                                          deobfuscated_name);
    return p.second || p.first->second == deobfuscated_name;
  }

  void AddMethod(MethodMapping mapping) {
    method_mappings_.push_back(std::move(mapping));
  }

  const std::vector<MethodMapping>& method_mappings() const {
    return method_mappings_;
  }

 private:
  std::string deobfuscated_name_;
  std::map<std::string, std::string> deobfuscated_fields_;
  std::vector<MethodMapping> method_mappings_;
};

class ProguardParser {
 public:
  // A return value of false means this line failed to parse. This leaves the
  // parser in an undefined state and it should no longer be used.
  base::Status AddLine(std::string line);
  bool AddLines(std::string contents);

  std::map<std::string, ObfuscatedClass> ConsumeMapping() {
    return std::move(mapping_);
  }

 private:
  std::map<std::string, ObfuscatedClass> mapping_;
  ObfuscatedClass* current_class_ = nullptr;
};

struct ProguardMap {
  std::string package;
  std::string filename;
};

void MakeDeobfuscationPackets(
    const std::string& package_name,
    const std::map<std::string, profiling::ObfuscatedClass>& mapping,
    std::function<void(const std::string&)> callback);

std::vector<ProguardMap> GetPerfettoProguardMapPath();

bool ReadProguardMapsToDeobfuscationPackets(
    const std::vector<ProguardMap>& maps,
    std::function<void(std::string)> fn);

}  // namespace profiling
}  // namespace perfetto

#endif  // SRC_TRACE_PROCESSOR_UTIL_DEOBFUSCATION_DEOBFUSCATOR_H_
