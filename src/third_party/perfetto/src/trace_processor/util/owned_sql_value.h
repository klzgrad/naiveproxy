/*
 * Copyright (C) 2026 The Android Open Source Project
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

#ifndef SRC_TRACE_PROCESSOR_UTIL_OWNED_SQL_VALUE_H_
#define SRC_TRACE_PROCESSOR_UTIL_OWNED_SQL_VALUE_H_

#include <cstddef>
#include <cstdint>
#include <string>
#include <variant>
#include <vector>

#include "perfetto/base/logging.h"
#include "perfetto/ext/base/variant.h"
#include "perfetto/trace_processor/basic_types.h"

namespace perfetto::trace_processor {

// An owning counterpart to SqlValue.
//
// SqlValue holds non-owning pointers to string/blob storage that must
// outlive it (e.g. SQLite function argument memory which is only valid for
// the duration of a single function call). OwnedSqlValue instead
// deep-copies any string or blob payload into its own storage, so it can
// be retained safely beyond the lifetime of the source.
class OwnedSqlValue {
 public:
  // variant is a pain to use, but it's the simplest way to ensure
  // destructors run correctly for non-trivial members of the union.
  using Data = std::variant<std::nullptr_t,
                            int64_t,
                            double,
                            std::string,
                            std::vector<uint8_t>>;

  OwnedSqlValue() = default;

  explicit OwnedSqlValue(const SqlValue& value) {
    switch (value.type) {
      case SqlValue::Type::kNull:
        data_ = nullptr;
        break;
      case SqlValue::Type::kLong:
        data_ = value.long_value;
        break;
      case SqlValue::Type::kDouble:
        data_ = value.double_value;
        break;
      case SqlValue::Type::kString:
        data_ = std::string(value.string_value ? value.string_value : "");
        break;
      case SqlValue::Type::kBytes: {
        const auto* ptr = static_cast<const uint8_t*>(value.bytes_value);
        data_ = std::vector<uint8_t>(ptr, ptr + value.bytes_count);
        break;
      }
    }
  }

  SqlValue::Type type() const {
    switch (data_.index()) {
      case base::variant_index<Data, std::nullptr_t>():
        return SqlValue::Type::kNull;
      case base::variant_index<Data, int64_t>():
        return SqlValue::Type::kLong;
      case base::variant_index<Data, double>():
        return SqlValue::Type::kDouble;
      case base::variant_index<Data, std::string>():
        return SqlValue::Type::kString;
      case base::variant_index<Data, std::vector<uint8_t>>():
        return SqlValue::Type::kBytes;
    }
    PERFETTO_FATAL("For GCC");
  }

  int64_t AsLong() const { return base::unchecked_get<int64_t>(data_); }
  double AsDouble() const { return base::unchecked_get<double>(data_); }
  const char* AsString() const {
    return base::unchecked_get<std::string>(data_).c_str();
  }
  const void* AsBytes() const {
    return base::unchecked_get<std::vector<uint8_t>>(data_).data();
  }
  size_t bytes_count() const {
    return base::unchecked_get<std::vector<uint8_t>>(data_).size();
  }

  // Returns a non-owning SqlValue view backed by this object's storage. The
  // pointers in the returned SqlValue are valid until this OwnedSqlValue is
  // destroyed, reassigned, or moved.
  SqlValue AsSqlValue() const {
    switch (data_.index()) {
      case base::variant_index<Data, std::nullptr_t>():
        return {};
      case base::variant_index<Data, int64_t>():
        return SqlValue::Long(AsLong());
      case base::variant_index<Data, double>():
        return SqlValue::Double(AsDouble());
      case base::variant_index<Data, std::string>():
        return SqlValue::String(AsString());
      case base::variant_index<Data, std::vector<uint8_t>>():
        return SqlValue::Bytes(AsBytes(), bytes_count());
    }
    PERFETTO_FATAL("Unreachable");
  }

 private:
  Data data_ = nullptr;
};

}  // namespace perfetto::trace_processor

#endif  // SRC_TRACE_PROCESSOR_UTIL_OWNED_SQL_VALUE_H_
