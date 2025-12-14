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

#ifndef SRC_TRACE_PROCESSOR_DATAFRAME_IMPL_BYTECODE_INTERPRETER_TEST_UTILS_H_
#define SRC_TRACE_PROCESSOR_DATAFRAME_IMPL_BYTECODE_INTERPRETER_TEST_UTILS_H_

#include <cstdint>
#include <variant>
#include <vector>

#include "perfetto/base/logging.h"
#include "perfetto/ext/base/string_utils.h"
#include "perfetto/ext/base/variant.h"
#include "src/trace_processor/dataframe/impl/bytecode_instructions.h"
#include "src/trace_processor/dataframe/impl/types.h"
#include "src/trace_processor/dataframe/specs.h"
#include "src/trace_processor/dataframe/value_fetcher.h"

namespace perfetto::trace_processor::dataframe::impl::bytecode {

using FilterValue = std::variant<int64_t, double, const char*, std::nullptr_t>;

struct Fetcher : ValueFetcher {
  using Type = size_t;
  [[maybe_unused]] static constexpr Type kInt64 =
      base::variant_index<FilterValue, int64_t>();
  [[maybe_unused]] static constexpr Type kDouble =
      base::variant_index<FilterValue, double>();
  [[maybe_unused]] static constexpr Type kString =
      base::variant_index<FilterValue, const char*>();
  [[maybe_unused]] static constexpr Type kNull =
      base::variant_index<FilterValue, std::nullptr_t>();

  int64_t GetInt64Value(uint32_t idx) const {
    PERFETTO_CHECK(idx == 0);
    return std::get<int64_t>(value[i]);
  }
  double GetDoubleValue(uint32_t idx) const {
    PERFETTO_CHECK(idx == 0);
    return std::get<double>(value[i]);
  }
  const char* GetStringValue(uint32_t idx) const {
    PERFETTO_CHECK(idx == 0);
    return std::get<const char*>(value[i]);
  }
  Type GetValueType(uint32_t idx) const {
    PERFETTO_CHECK(idx == 0);
    return value[i].index();
  }
  bool IteratorInit(uint32_t idx) {
    PERFETTO_CHECK(idx == 0);
    i = 0;
    return i < value.size();
  }
  bool IteratorNext(uint32_t idx) {
    PERFETTO_CHECK(idx == 0);
    i++;
    return i < value.size();
  }

  std::vector<FilterValue> value;
  uint32_t i = 0;
};

inline std::string FixNegativeAndDecimalAndDouble(const std::string& str) {
  std::vector<std::string> replace_with_underscore = {".", "(", ")"};
  std::string result = str;
  for (const auto& to_replace : replace_with_underscore) {
    result = base::ReplaceAll(result, to_replace, "_");
  }
  return base::ReplaceAll(result, "-", "neg_");
}

inline std::string ValToString(const FilterValue& value) {
  switch (value.index()) {
    case base::variant_index<FilterValue, std::nullptr_t>():
      return "nullptr";
    case base::variant_index<FilterValue, int64_t>(): {
      auto res = base::unchecked_get<int64_t>(value);
      return FixNegativeAndDecimalAndDouble(std::to_string(res));
    }
    case base::variant_index<FilterValue, double>(): {
      auto res = base::unchecked_get<double>(value);
      return FixNegativeAndDecimalAndDouble(std::to_string(res));
    }
    case base::variant_index<FilterValue, const char*>():
      return {base::unchecked_get<const char*>(value)};
    default:
      PERFETTO_FATAL("Unknown filter value type");
  }
}

inline std::string OpToString(const Op& op) {
  switch (op.index()) {
    case Op::GetTypeIndex<Eq>():
      return "Eq";
    case Op::GetTypeIndex<Ne>():
      return "Ne";
    case Op::GetTypeIndex<Lt>():
      return "Lt";
    case Op::GetTypeIndex<Le>():
      return "Le";
    case Op::GetTypeIndex<Gt>():
      return "Gt";
    case Op::GetTypeIndex<Ge>():
      return "Ge";
    case Op::GetTypeIndex<Glob>():
      return "Glob";
    case Op::GetTypeIndex<Regex>():
      return "Regex";
    default:
      PERFETTO_FATAL("Unknown op");
  }
}

inline std::string ResultToString(const CastFilterValueResult& res) {
  if (res.validity == CastFilterValueResult::Validity::kValid) {
    switch (res.value.index()) {
      case base::variant_index<CastFilterValueResult::Value,
                               CastFilterValueResult::Id>(): {
        const auto& id =
            base::unchecked_get<CastFilterValueResult::Id>(res.value);
        return "Id_" + FixNegativeAndDecimalAndDouble(std::to_string(id.value));
      }
      case base::variant_index<CastFilterValueResult::Value, uint32_t>(): {
        const auto& uint32 = base::unchecked_get<uint32_t>(res.value);
        return "Uint32_" +
               FixNegativeAndDecimalAndDouble(std::to_string(uint32));
      }
      case base::variant_index<CastFilterValueResult::Value, int32_t>(): {
        const auto& int32 = base::unchecked_get<int32_t>(res.value);
        return "Int32_" + FixNegativeAndDecimalAndDouble(std::to_string(int32));
      }
      case base::variant_index<CastFilterValueResult::Value, int64_t>(): {
        const auto& int64 = base::unchecked_get<int64_t>(res.value);
        return "Int64_" + FixNegativeAndDecimalAndDouble(std::to_string(int64));
      }
      case base::variant_index<CastFilterValueResult::Value, double>(): {
        const auto& d = base::unchecked_get<double>(res.value);
        return "Double_" + FixNegativeAndDecimalAndDouble(std::to_string(d));
      }
      case base::variant_index<CastFilterValueResult::Value, const char*>(): {
        return base::unchecked_get<const char*>(res.value);
      }
      default:
        PERFETTO_FATAL("Unknown filter value type");
    }
  }
  return res.validity == CastFilterValueResult::Validity::kNoneMatch
             ? "NoneMatch"
             : "AllMatch";
}

template <typename T>
inline Span<T> GetSpan(std::vector<T>& vec) {
  return Span<T>{vec.data(), vec.data() + vec.size()};
}

inline Bytecode ParseBytecode(const std::string& bytecode_str) {
  static constexpr uint32_t kNumBytecodeCount =
      std::variant_size_v<BytecodeVariant>;

#define PERFETTO_DATAFRAME_BYTECODE_AS_STRING(...) #__VA_ARGS__,
  static constexpr std::array<const char*, kNumBytecodeCount> bytecode_names{
      PERFETTO_DATAFRAME_BYTECODE_LIST(PERFETTO_DATAFRAME_BYTECODE_AS_STRING)};

#define PERFETTO_DATAFRAME_BYTECODE_OFFSETS(...) __VA_ARGS__::kOffsets,
  static constexpr std::array<std::array<uint32_t, 9>, kNumBytecodeCount>
      offsets{PERFETTO_DATAFRAME_BYTECODE_LIST(
          PERFETTO_DATAFRAME_BYTECODE_OFFSETS)};

#define PERFETTO_DATAFRAME_BYTECODE_NAMES(...) __VA_ARGS__::kNames,
  static constexpr std::array<std::array<const char*, 8>, kNumBytecodeCount>
      names{
          PERFETTO_DATAFRAME_BYTECODE_LIST(PERFETTO_DATAFRAME_BYTECODE_NAMES)};

  Bytecode bc;
  size_t colon_pos = bytecode_str.find(": ");
  PERFETTO_CHECK(colon_pos != std::string::npos);
  {
    const auto* it = std::find(bytecode_names.data(),
                               bytecode_names.data() + bytecode_names.size(),
                               bytecode_str.substr(0, colon_pos));
    PERFETTO_CHECK(it != bytecode_names.data() + bytecode_names.size());
    bc.option = static_cast<uint32_t>(it - bytecode_names.data());
  }

  // Trim away the [ and ] from the bytecode string.
  std::string args_str = bytecode_str.substr(colon_pos + 2);
  PERFETTO_CHECK(args_str.front() == '[');
  PERFETTO_CHECK(args_str.back() == ']');
  args_str = args_str.substr(1, args_str.size() - 2);

  const auto& cur_offset = offsets[bc.option];
  std::vector<std::string> args = base::SplitString(args_str, ", ");
  for (const auto& arg : args) {
    size_t eq_pos = arg.find('=');
    PERFETTO_CHECK(eq_pos != std::string::npos);
    std::string arg_name = arg.substr(0, eq_pos);
    std::string arg_val = arg.substr(eq_pos + 1);

    // Remove everything before the first "(" (which may not be the first
    // character) and after the last ")".
    if (size_t open = arg_val.find('('); open != std::string_view::npos) {
      arg_val = arg_val.substr(open + 1, arg_val.rfind(')') - open - 1);
    }

    const auto& n = names[bc.option];
    const auto* it = std::find(n.data(), n.data() + n.size(), arg_name);
    PERFETTO_CHECK(it != n.data() + n.size());
    auto arg_idx = static_cast<uint32_t>(it - n.data());
    uint32_t size = cur_offset[arg_idx + 1] - cur_offset[arg_idx];
    if (size == 2) {
      auto val = base::StringToInt64(arg_val);
      PERFETTO_CHECK(val.has_value());
      auto cast = static_cast<uint16_t>(*val);
      memcpy(&bc.args_buffer[cur_offset[arg_idx]], &cast, 2);
    } else if (size == 4) {
      auto val = base::StringToInt64(arg_val);
      PERFETTO_CHECK(val.has_value());
      auto cast = static_cast<uint32_t>(*val);
      memcpy(&bc.args_buffer[cur_offset[arg_idx]], &cast, 4);
    } else if (size == 8) {
      auto val = base::StringToInt64(arg_val);
      PERFETTO_CHECK(val.has_value());
      memcpy(&bc.args_buffer[cur_offset[arg_idx]], &val, 8);
    } else {
      PERFETTO_CHECK(false);
    }
  }
  return bc;
}

template <typename T, typename U>
inline Column CreateNonNullColumn(std::initializer_list<U> data,
                                  SortState sort_state,
                                  DuplicateState duplicate_state) {
  impl::FlexVector<T> vec;
  for (const U& val : data) {
    vec.push_back(val);
  }
  return impl::Column{impl::Storage{std::move(vec)},
                      impl::NullStorage::NonNull{}, sort_state,
                      duplicate_state};
}

template <typename U>
inline Column CreateNonNullStringColumn(std::initializer_list<U> data,
                                        SortState sort_state,
                                        DuplicateState duplicate_state,
                                        StringPool* pool) {
  PERFETTO_CHECK(pool);
  impl::FlexVector<StringPool::Id> vec;
  for (const auto& str_like : data) {
    vec.push_back(pool->InternString(str_like));
  }
  return impl::Column{impl::Storage{std::move(vec)},
                      impl::NullStorage::NonNull{}, sort_state,
                      duplicate_state};
}

template <typename T>
inline FlexVector<T> CreateFlexVectorForTesting(
    std::initializer_list<T> values) {
  FlexVector<T> vec;
  for (const auto& value : values) {
    vec.push_back(value);
  }
  return vec;
}

template <typename T>
inline Column CreateSparseNullableColumn(
    const std::vector<std::optional<T>>& data_with_nulls,
    SortState sort_state,
    DuplicateState duplicate_state) {
  auto num_rows = static_cast<uint32_t>(data_with_nulls.size());
  auto data_vec = FlexVector<T>::CreateWithCapacity(num_rows);
  auto bv = BitVector::CreateWithSize(num_rows);
  for (uint32_t i = 0; i < num_rows; ++i) {
    if (data_with_nulls[i].has_value()) {
      data_vec.push_back(*data_with_nulls[i]);
      bv.set(i);
    }
  }
  return impl::Column{
      impl::Storage{std::move(data_vec)},
      impl::NullStorage{impl::NullStorage::SparseNull{std::move(bv), {}}},
      sort_state, duplicate_state};
}

inline Column CreateSparseNullableStringColumn(
    const std::vector<std::optional<const char*>>& data_with_nulls,
    StringPool* pool,
    SortState sort_state,
    DuplicateState duplicate_state) {
  auto num_rows = static_cast<uint32_t>(data_with_nulls.size());
  auto data_vec = FlexVector<StringPool::Id>::CreateWithCapacity(num_rows);
  auto bv = BitVector::CreateWithSize(num_rows);
  for (uint32_t i = 0; i < num_rows; ++i) {
    if (data_with_nulls[i].has_value()) {
      data_vec.push_back(pool->InternString(*data_with_nulls[i]));
      bv.set(i);
    }
  }
  return impl::Column{
      impl::Storage{std::move(data_vec)},
      impl::NullStorage{impl::NullStorage::SparseNull{std::move(bv), {}}},
      sort_state, duplicate_state};
}

template <typename T>
inline Column CreateDenseNullableColumn(
    const std::vector<std::optional<T>>& data_with_nulls,
    SortState sort_state,
    DuplicateState duplicate_state) {
  auto num_rows = static_cast<uint32_t>(data_with_nulls.size());
  auto data_vec = FlexVector<T>::CreateWithSize(num_rows);
  auto bv = BitVector::CreateWithSize(num_rows);

  for (uint32_t i = 0; i < num_rows; ++i) {
    if (data_with_nulls[i].has_value()) {
      data_vec[i] = *data_with_nulls[i];
      bv.set(i);
    } else {
      data_vec[i] = T{};  // Default construct T for null storage slot
    }
  }
  return impl::Column{
      impl::Storage{std::move(data_vec)},
      impl::NullStorage{impl::NullStorage::DenseNull{std::move(bv)}},
      sort_state, duplicate_state};
}

inline Column CreateDenseNullableStringColumn(
    const std::vector<std::optional<const char*>>& data_with_nulls,
    StringPool* pool,
    SortState sort_state,
    DuplicateState duplicate_state) {
  auto num_rows = static_cast<uint32_t>(data_with_nulls.size());
  auto data_vec = FlexVector<StringPool::Id>::CreateWithSize(num_rows);
  auto bv = BitVector::CreateWithSize(num_rows);

  for (uint32_t i = 0; i < num_rows; ++i) {
    if (data_with_nulls[i].has_value()) {
      data_vec[i] = pool->InternString(*data_with_nulls[i]);
      bv.set(i);
    } else {
      data_vec[i] = StringPool::Id::Null();
    }
  }
  return impl::Column{
      impl::Storage{std::move(data_vec)},
      impl::NullStorage{impl::NullStorage::DenseNull{std::move(bv)}},
      sort_state, duplicate_state};
}

PERFETTO_NO_INLINE BytecodeVector inline ParseBytecodeToVec(
    const std::string& bytecode_str) {
  BytecodeVector bytecode_vector;
  std::vector<std::string> lines = base::SplitString(bytecode_str, "\n");
  for (const auto& line : lines) {
    std::string trimmed = base::TrimWhitespace(line);
    if (!trimmed.empty()) {
      bytecode_vector.emplace_back(ParseBytecode(trimmed));
    }
  }
  return bytecode_vector;
}

}  // namespace perfetto::trace_processor::dataframe::impl::bytecode

#endif  // SRC_TRACE_PROCESSOR_DATAFRAME_IMPL_BYTECODE_INTERPRETER_TEST_UTILS_H_
