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

#ifndef SRC_TRACE_PROCESSOR_UTIL_SIMPLE_JSON_PARSER_H_
#define SRC_TRACE_PROCESSOR_UTIL_SIMPLE_JSON_PARSER_H_

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

#include "perfetto/base/status.h"
#include "perfetto/ext/base/status_macros.h"
#include "perfetto/ext/base/status_or.h"
#include "src/trace_processor/util/json_parser.h"

namespace perfetto::trace_processor::json {

// Result type for ForEachField callbacks indicating whether a field was
// handled.
struct FieldResult {
  struct Handled {};
  struct Skip {};

  FieldResult(Handled) : handled(true) {}
  FieldResult(Skip) : handled(false) {}
  FieldResult(base::Status s) : handled(true), status(std::move(s)) {}

  bool handled = false;
  base::Status status;
};

// Helper class for JSON parsing with a cleaner API.
// This wraps json::Iterator and provides convenient methods for:
// - Iterating over object fields
// - Iterating over array elements
// - Type-safe value extraction
//
// Example usage:
//   SimpleJsonParser parser(json_string);
//   RETURN_IF_ERROR(parser.Parse());
//   RETURN_IF_ERROR(parser.ForEachField([&](std::string_view key) ->
//   FieldResult {
//     if (key == "name") {
//       name = parser.GetString().value_or("");
//       return FieldResult::Handled{};
//     } else if (key == "items") {
//       RETURN_IF_ERROR(parser.ForEachArrayElement([&]() {
//         items.push_back(parser.GetUint32().value_or(0));
//         return base::OkStatus();
//       }));
//       return FieldResult::Handled{};
//     }
//     return FieldResult::Skip{};  // Unknown field, auto-skip
//   }));
class SimpleJsonParser {
 public:
  explicit SimpleJsonParser(std::string_view json)
      : json_(json.data(), json.size()) {
    it_.Reset(json_.data(), json_.data() + json_.size());
  }

  // Parse the start of the JSON (must be called first).
  base::Status Parse() {
    if (!it_.ParseStart()) {
      return base::ErrStatus("Failed to parse JSON: %s",
                             it_.status().message().c_str());
    }
    return base::OkStatus();
  }

  // Current field key (valid after ForEachField callback is invoked).
  std::string_view key() const { return it_.key(); }

  // Current value (valid after ForEachField/ForEachArrayElement callback).
  const JsonValue& value() const { return it_.value(); }

  // Type-safe value getters.
  std::optional<int64_t> GetInt64() const {
    if (const auto* i = std::get_if<int64_t>(&it_.value())) {
      return *i;
    }
    if (const auto* d = std::get_if<double>(&it_.value())) {
      return static_cast<int64_t>(*d);
    }
    return std::nullopt;
  }

  std::optional<uint32_t> GetUint32() const {
    if (const auto* i = std::get_if<int64_t>(&it_.value())) {
      return *i >= 0 ? std::make_optional(static_cast<uint32_t>(*i))
                     : std::nullopt;
    }
    if (const auto* d = std::get_if<double>(&it_.value())) {
      return *d >= 0 ? std::make_optional(static_cast<uint32_t>(*d))
                     : std::nullopt;
    }
    return std::nullopt;
  }

  std::optional<double> GetDouble() const {
    if (const auto* d = std::get_if<double>(&it_.value())) {
      return *d;
    }
    if (const auto* i = std::get_if<int64_t>(&it_.value())) {
      return static_cast<double>(*i);
    }
    return std::nullopt;
  }

  std::optional<std::string_view> GetString() const {
    if (const auto* s = std::get_if<std::string_view>(&it_.value())) {
      return *s;
    }
    return std::nullopt;
  }

  std::optional<bool> GetBool() const {
    if (const auto* b = std::get_if<bool>(&it_.value())) {
      return *b;
    }
    return std::nullopt;
  }

  bool IsNull() const { return std::holds_alternative<Null>(it_.value()); }

  bool IsObject() const { return std::holds_alternative<Object>(it_.value()); }

  bool IsArray() const { return std::holds_alternative<Array>(it_.value()); }

  // Iterate over fields of the current object.
  // The callback receives the field key and should return:
  //   - FieldResult::Handled{} if the field was processed
  //   - FieldResult::Skip{} to skip the field (nested content auto-skipped)
  //   - base::Status for errors
  // After each callback, the value is available via GetXxx() methods.
  // For nested objects/arrays, call ForEachField/ForEachArrayElement
  // recursively before returning Handled.
  template <typename Fn>
  base::Status ForEachField(Fn&& fn) {
    while (true) {
      auto rc = it_.ParseAndRecurse();
      if (rc == ReturnCode::kEndOfScope) {
        break;
      }
      if (rc != ReturnCode::kOk) {
        return base::ErrStatus("Error parsing JSON object field: %s",
                               it_.status().message().c_str());
      }
      FieldResult result = fn(it_.key());
      RETURN_IF_ERROR(result.status);
      // If callback didn't handle and value is nested, skip it.
      if (!result.handled && (IsObject() || IsArray())) {
        RETURN_IF_ERROR(SkipCurrentScope());
      }
    }
    return base::OkStatus();
  }

  // Iterate over elements of the current array.
  // The callback should return OkStatus to continue.
  // After each callback, the value is available via GetXxx() methods.
  // For nested objects/arrays, call ForEachField/ForEachArrayElement
  // recursively. Unconsumed nested structures are NOT auto-skipped for arrays.
  template <typename Fn>
  base::Status ForEachArrayElement(Fn&& fn) {
    while (true) {
      auto rc = it_.ParseAndRecurse();
      if (rc == ReturnCode::kEndOfScope) {
        break;
      }
      if (rc != ReturnCode::kOk) {
        return base::ErrStatus("Error parsing JSON array element: %s",
                               it_.status().message().c_str());
      }
      RETURN_IF_ERROR(fn());
    }
    return base::OkStatus();
  }

  // Convenience: collect all uint32 values from an array into a vector.
  base::StatusOr<std::vector<uint32_t>> CollectUint32Array();

  // Convenience: collect all int64 values from an array into a vector.
  base::StatusOr<std::vector<int64_t>> CollectInt64Array();

  // Convenience: collect all string values from an array into a vector.
  base::StatusOr<std::vector<std::string>> CollectStringArray();

  // Convenience: collect all double values from an array into a vector.
  base::StatusOr<std::vector<double>> CollectDoubleArray();

 private:
  // Skip the current nested object/array scope.
  // Called when callback returns Skip for a nested value.
  // Uses ParseObjectFieldWithoutRecursing for objects (efficient - skips nested
  // content via ScanToEndOfDelimitedBlock) and ParseAndRecurse for arrays.
  base::Status SkipCurrentScope() {
    size_t target_depth = it_.parse_stack().size() - 1;
    while (it_.parse_stack().size() > target_depth) {
      ReturnCode rc;
      if (it_.parse_stack().back() == Iterator::ParseType::kObject) {
        rc = it_.ParseObjectFieldWithoutRecursing();
      } else {
        rc = it_.ParseAndRecurse();
      }
      if (rc == ReturnCode::kEndOfScope) {
        continue;  // Keep going until we exit the scope.
      }
      if (rc != ReturnCode::kOk) {
        return base::ErrStatus("Error skipping JSON value: %s",
                               it_.status().message().c_str());
      }
    }
    return base::OkStatus();
  }

  std::string json_;  // Keep a copy since iterator uses pointers.
  Iterator it_;
};

}  // namespace perfetto::trace_processor::json

#endif  // SRC_TRACE_PROCESSOR_UTIL_SIMPLE_JSON_PARSER_H_
