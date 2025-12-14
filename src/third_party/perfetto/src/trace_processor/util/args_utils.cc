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

#include "src/trace_processor/util/args_utils.h"

#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "perfetto/base/logging.h"
#include "perfetto/base/status.h"
#include "perfetto/ext/base/string_splitter.h"
#include "perfetto/ext/base/string_utils.h"
#include "perfetto/ext/base/string_view.h"
#include "perfetto/public/compiler.h"
#include "src/trace_processor/containers/null_term_string_view.h"
#include "src/trace_processor/types/variadic.h"

namespace perfetto::trace_processor {

ArgNode::ArgNode(Variadic value)
    : type_(Type::kPrimitive), primitive_value_(value) {}

// static
ArgNode ArgNode::Array() {
  ArgNode node(Variadic::Null());
  node.type_ = Type::kArray;
  node.array_ = std::make_unique<std::vector<ArgNode>>();
  node.primitive_value_ = Variadic::Null();
  return node;
}

// static
ArgNode ArgNode::Dict() {
  ArgNode node(Variadic::Null());
  node.type_ = Type::kDict;
  node.dict_ = std::make_unique<std::vector<std::pair<std::string, ArgNode>>>();
  node.dict_index_ = std::make_unique<base::FlatHashMap<std::string, size_t>>();
  node.primitive_value_ = Variadic::Null();
  return node;
}

Variadic ArgNode::GetPrimitiveValue() const {
  PERFETTO_CHECK(type_ == Type::kPrimitive);
  return primitive_value_;
}

const std::vector<ArgNode>& ArgNode::GetArray() const {
  PERFETTO_CHECK(type_ == Type::kArray);
  PERFETTO_CHECK(array_);
  return *array_;
}

const std::vector<std::pair<std::string, ArgNode>>& ArgNode::GetDict() const {
  PERFETTO_CHECK(type_ == Type::kDict);
  PERFETTO_CHECK(dict_);
  return *dict_;
}

ArgNode& ArgNode::AppendOrGet(size_t index) {
  PERFETTO_CHECK(type_ == Type::kArray);
  while (array_->size() <= index) {
    array_->push_back(ArgNode(Variadic::Null()));
  }
  return (*array_)[index];
}

ArgNode& ArgNode::AddOrGet(const std::string_view key) {
  PERFETTO_CHECK(type_ == Type::kDict);
  PERFETTO_CHECK(dict_index_);

  // Fast O(1) lookup in hash map
  std::string key_str(key);  // Need std::string for hash map
  if (size_t* idx = dict_index_->Find(key_str)) {
    return (*dict_)[*idx].second;
  }

  // Not found - add new entry
  size_t new_idx = dict_->size();
  dict_->emplace_back(std::move(key_str), ArgNode(Variadic::Null()));
  dict_index_->Insert(dict_->back().first, new_idx);
  return dict_->back().second;
}

void ArgNode::Clear() {
  switch (type_) {
    case Type::kPrimitive:
      primitive_value_ = Variadic::Null();
      break;
    case Type::kArray:
      if (array_) {
        array_->clear();  // Clear but retain capacity
      }
      break;
    case Type::kDict:
      if (dict_) {
        dict_->clear();  // Clear but retain capacity
      }
      if (dict_index_) {
        dict_index_->Clear();  // Clear but retain capacity
      }
      break;
  }
}

ArgSet::ArgSet() : root_(ArgNode::Dict()) {}

void ArgSet::Clear() {
  root_.Clear();
}

base::Status ArgSet::AppendArg(NullTermStringView key, Variadic value) {
  // Parse the key path (e.g., "foo.bar[0].baz")
  ArgNode* target = &root_;

  for (base::StringSplitter parts(key.c_str(), '.'); parts.Next();) {
    std::string_view part{parts.cur_token(), parts.cur_token_size()};
    if (target->IsNull()) {
      *target = ArgNode::Dict();
    }
    if (target->GetType() != ArgNode::Type::kDict) {
      return base::ErrStatus(
          "Failed to insert key %s: tried to insert %s into a non-dictionary "
          "object",
          key.c_str(), std::string(part).c_str());
    }
    size_t bracket_pos = part.find('[');
    if (bracket_pos == std::string::npos) {
      // A single item.
      target = &target->AddOrGet(part);
    } else {
      target = &target->AddOrGet(part.substr(0, bracket_pos));
      while (bracket_pos != std::string::npos) {
        // We constructed this string from an int earlier in trace_processor
        // so it shouldn't be possible for this (or the StringViewToUInt32
        // below) to fail.
        std::string_view s = part.substr(
            bracket_pos + 1, part.find(']', bracket_pos) - bracket_pos - 1);
        std::optional<uint32_t> index =
            base::StringViewToUInt32(base::StringView(s));
        if (PERFETTO_UNLIKELY(!index)) {
          return base::ErrStatus(
              "Expected to be able to extract index from %s of key %s",
              std::string(part).c_str(), key.c_str());
        }
        if (target->IsNull()) {
          *target = ArgNode::Array();
        }
        if (target->GetType() != ArgNode::Type::kArray) {
          return base::ErrStatus(
              "Failed to insert key %s: tried to insert %s into a non-array"
              "object",
              key.c_str(), std::string(part).c_str());
        }
        target = &target->AppendOrGet(index.value());
        bracket_pos = part.find('[', bracket_pos + 1);
      }
    }
  }
  *target = ArgNode(value);
  return base::OkStatus();
}

}  // namespace perfetto::trace_processor
