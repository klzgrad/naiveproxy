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

#include "src/trace_processor/perfetto_sql/intrinsics/functions/replace_numbers_function.h"

#include <cctype>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <optional>
#include <string>

#include "perfetto/base/status.h"
#include "perfetto/trace_processor/basic_types.h"
#include "src/trace_processor/perfetto_sql/engine/perfetto_sql_engine.h"
#include "src/trace_processor/sqlite/bindings/sqlite_function.h"
#include "src/trace_processor/sqlite/bindings/sqlite_result.h"
#include "src/trace_processor/sqlite/bindings/sqlite_type.h"
#include "src/trace_processor/sqlite/bindings/sqlite_value.h"
#include "src/trace_processor/sqlite/sqlite_utils.h"
#include "src/trace_processor/types/trace_processor_context.h"

namespace perfetto::trace_processor {
namespace {

// __intrinsic_strip_hex(name STRING, min_repeated_digits LONG)
//
//   Replaces hexadecimal sequences (with at least one digit) in a string with
//   "<num>" based on specified criteria.
struct StripHexFunction : public sqlite::Function<StripHexFunction> {
  static constexpr char kName[] = "__intrinsic_strip_hex";
  static constexpr int kArgCount = 2;

  static void Step(sqlite3_context* ctx, int argc, sqlite3_value** argv) {
    PERFETTO_DCHECK(argc == 2);

    const char* input = nullptr;
    switch (sqlite::value::Type(argv[0])) {
      case sqlite::Type::kText:
        input = sqlite::value::Text(argv[0]);
        break;
      case sqlite::Type::kNull:
        return sqlite::utils::ReturnNullFromFunction(ctx);
      case sqlite::Type::kInteger:
      case sqlite::Type::kFloat:
      case sqlite::Type::kBlob:
        return sqlite::utils::SetError(
            ctx, "__intrinsic_strip_hex: first argument must be string");
    }

    int64_t min_repeated_digits = 0;
    switch (sqlite::value::Type(argv[1])) {
      case sqlite::Type::kInteger:
        min_repeated_digits = sqlite::value::Int64(argv[1]);
        break;
      case sqlite::Type::kNull:
        return sqlite::utils::ReturnNullFromFunction(ctx);
      case sqlite::Type::kFloat:
      case sqlite::Type::kText:
      case sqlite::Type::kBlob:
        return sqlite::utils::SetError(
            ctx, "__intrinsic_strip_hex: second argument must be integer");
    }
    if (min_repeated_digits < 0) {
      return sqlite::utils::SetError(
          ctx, "__intrinsic_strip_hex: min_repeated_digits must be positive");
    }

    std::string result = StripHex(std::string(input), min_repeated_digits);
    return sqlite::result::TransientString(ctx, result.c_str(),
                                           static_cast<int>(result.length()));
  }

  static std::string StripHex(const std::string& input,
                              int64_t min_repeated_digits) {
    std::string result;
    result.reserve(input.length());
    for (size_t i = 0; i < input.length();) {
      bool replace_hex = false;
      if ((input[i] == 'x' || input[i] == 'X') && i >= 1 &&
          input[i - 1] == '0') {
        // Case 1: Special prefixes (0x, 0X) for hex sequence found
        result += input[i++];
        // Always try to replace hex after 0x, regardless if they contain digits
        // or not
        replace_hex = true;
      } else if (!isalnum(input[i])) {
        // Case 2: Non alpha numeric prefix for hex sequence found
        result += input[i++];
      } else if (i == 0 && isxdigit(input[i])) {
        // Case 3: Start of input is hex digit, continue to check hex sequence
      } else if (isdigit(input[i])) {
        // Case 4: A digit is found, consider replacing the sequence
      } else {
        // Case 5: No potential prefix for hex digits found
        result += input[i++];
        continue;
      }

      size_t hex_start = i;
      for (; i < input.length() && isxdigit(input[i]); i++) {
        if (isdigit(input[i])) {
          replace_hex = true;
        }
      }
      result += replace_hex && (i - hex_start >=
                                static_cast<size_t>(min_repeated_digits))
                    ? "<num>"
                    : input.substr(hex_start, i - hex_start);
    }
    return result;
  }
};

}  // namespace

base::Status RegisterStripHexFunction(PerfettoSqlEngine* engine,
                                      TraceProcessorContext*) {
  return engine->RegisterFunction<StripHexFunction>(nullptr);
}

std::string SqlStripHex(const std::string& input, int64_t min_repeated_digits) {
  return StripHexFunction::StripHex(input, min_repeated_digits);
}

}  // namespace perfetto::trace_processor
