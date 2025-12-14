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

#ifndef SRC_TRACE_PROCESSOR_UTIL_ARGS_UTILS_H_
#define SRC_TRACE_PROCESSOR_UTIL_ARGS_UTILS_H_

#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "perfetto/base/status.h"
#include "src/trace_processor/types/variadic.h"

namespace perfetto::trace_processor {
class TraceStorage;

class ArgSet;

class ArgNode {
 public:
  enum class Type { kPrimitive, kArray, kDict };

  ArgNode(ArgNode&& other) noexcept = default;
  ArgNode& operator=(ArgNode&& other) noexcept = default;
  ArgNode(const ArgNode&) = delete;
  ArgNode& operator=(const ArgNode&) = delete;

  bool IsNull() const {
    return type_ == Type::kPrimitive &&
           primitive_value_.type == Variadic::kNull;
  }
  Type GetType() const { return type_; }
  Variadic GetPrimitiveValue() const;
  const std::vector<ArgNode>& GetArray() const;
  const std::vector<std::pair<std::string, ArgNode>>& GetDict() const;

  // Clears the node while retaining allocated capacity for reuse.
  void Clear();

 private:
  ArgNode();
  explicit ArgNode(Variadic value);
  static ArgNode Array();
  static ArgNode Dict();

  ArgNode& AddOrGet(std::string_view key);
  ArgNode& AppendOrGet(size_t index);

  friend class ArgSet;
  Type type_;

  Variadic primitive_value_;
  std::unique_ptr<std::vector<ArgNode>> array_;
  // Use vector of pairs to preserve insertion order.
  std::unique_ptr<std::vector<std::pair<std::string, ArgNode>>> dict_;
  // Index for O(1) lookup in dict_. Maps key -> index in dict_ vector.
  std::unique_ptr<base::FlatHashMap<std::string, size_t>> dict_index_;
};

class ArgSet {
 public:
  ArgSet();
  ArgSet(ArgSet&& other) noexcept = default;
  ArgSet& operator=(ArgSet&& other) noexcept = default;
  ArgSet(const ArgSet&) = delete;
  ArgSet& operator=(const ArgSet&) = delete;

  const ArgNode& root() const { return root_; }

  base::Status AppendArg(NullTermStringView key, Variadic value);

  // Clears the arg set while retaining allocated capacity for reuse.
  void Clear();

 private:
  ArgNode root_;
};

}  // namespace perfetto::trace_processor

#endif  // SRC_TRACE_PROCESSOR_UTIL_ARGS_UTILS_H_
