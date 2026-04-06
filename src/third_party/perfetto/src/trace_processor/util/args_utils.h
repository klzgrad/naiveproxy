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

#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "perfetto/base/status.h"
#include "perfetto/ext/base/flat_hash_map.h"
#include "src/trace_processor/containers/null_term_string_view.h"
#include "src/trace_processor/containers/string_pool.h"
#include "src/trace_processor/core/dataframe/specs.h"
#include "src/trace_processor/storage/trace_storage.h"
#include "src/trace_processor/tables/metadata_tables_py.h"
#include "src/trace_processor/types/variadic.h"

namespace perfetto::trace_processor {

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

// A reusable cursor for looking up args by arg_set_id and key.
// This avoids creating a new cursor for each lookup.
class ArgExtractor {
 public:
  explicit ArgExtractor(const tables::ArgTable& arg_table)
      : cursor_(arg_table.CreateCursor(
            {dataframe::FilterSpec{tables::ArgTable::ColumnIndex::arg_set_id, 0,
                                   dataframe::Eq{}, std::nullopt},
             dataframe::FilterSpec{tables::ArgTable::ColumnIndex::key, 1,
                                   dataframe::Eq{}, std::nullopt}})) {}

  // Looks up an arg row by arg_set_id and key.
  // Returns std::numeric_limits<uint32_t>::max() if not found.
  uint32_t Get(uint32_t arg_set_id, const char* key) {
    cursor_.SetFilterValueUnchecked(0, arg_set_id);
    cursor_.SetFilterValueUnchecked(1, key);
    cursor_.Execute();
    return cursor_.Eof() ? std::numeric_limits<uint32_t>::max()
                         : cursor_.ToRowNumber().row_number();
  }

  // Access to the underlying cursor for retrieving arg values.
  const tables::ArgTable::ConstCursor& cursor() const { return cursor_; }

 private:
  tables::ArgTable::ConstCursor cursor_;
};

// Gets the Variadic value for an arg at the given row.
inline Variadic GetArgValue(const TraceStorage& storage,
                            const tables::ArgTable::ConstCursor& cursor) {
  Variadic v = Variadic::Null();
  v.type = *storage.GetVariadicTypeForId(cursor.value_type());
  switch (v.type) {
    case Variadic::Type::kBool:
      v.bool_value = static_cast<bool>(*cursor.int_value());
      break;
    case Variadic::Type::kInt:
      v.int_value = *cursor.int_value();
      break;
    case Variadic::Type::kUint:
      v.uint_value = static_cast<uint64_t>(*cursor.int_value());
      break;
    case Variadic::Type::kString: {
      auto opt_value = cursor.string_value();
      v.string_value = opt_value ? *opt_value : kNullStringId;
      break;
    }
    case Variadic::Type::kPointer:
      v.pointer_value = static_cast<uint64_t>(*cursor.int_value());
      break;
    case Variadic::Type::kReal:
      v.real_value = *cursor.real_value();
      break;
    case Variadic::Type::kJson: {
      auto opt_value = cursor.string_value();
      v.json_value = opt_value ? *opt_value : kNullStringId;
      break;
    }
    case Variadic::Type::kNull:
      break;
  }
  return v;
}

// Gets the Variadic value for an arg at the given row.
inline Variadic GetArgValue(const TraceStorage& storage, uint32_t row_index) {
  const auto& args = storage.arg_table();
  auto rr = args[row_index];
  Variadic v = Variadic::Null();
  v.type = *storage.GetVariadicTypeForId(rr.value_type());
  switch (v.type) {
    case Variadic::Type::kBool:
      v.bool_value = static_cast<bool>(*rr.int_value());
      break;
    case Variadic::Type::kInt:
      v.int_value = *rr.int_value();
      break;
    case Variadic::Type::kUint:
      v.uint_value = static_cast<uint64_t>(*rr.int_value());
      break;
    case Variadic::Type::kString: {
      auto opt_value = rr.string_value();
      v.string_value = opt_value ? *opt_value : kNullStringId;
      break;
    }
    case Variadic::Type::kPointer:
      v.pointer_value = static_cast<uint64_t>(*rr.int_value());
      break;
    case Variadic::Type::kReal:
      v.real_value = *rr.real_value();
      break;
    case Variadic::Type::kJson: {
      auto opt_value = rr.string_value();
      v.json_value = opt_value ? *opt_value : kNullStringId;
      break;
    }
    case Variadic::Type::kNull:
      break;
  }
  return v;
}

}  // namespace perfetto::trace_processor

#endif  // SRC_TRACE_PROCESSOR_UTIL_ARGS_UTILS_H_
