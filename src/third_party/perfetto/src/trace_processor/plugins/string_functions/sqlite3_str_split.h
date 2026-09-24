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

#ifndef SRC_TRACE_PROCESSOR_PLUGINS_STRING_FUNCTIONS_SQLITE3_STR_SPLIT_H_
#define SRC_TRACE_PROCESSOR_PLUGINS_STRING_FUNCTIONS_SQLITE3_STR_SPLIT_H_

#include "src/trace_processor/sqlite/bindings/sqlite_function.h"

namespace perfetto::trace_processor::string_functions {

// str_split(str, delimiter, field) returns the field-th (0-based) component
// of |str| split on |delimiter|, or NULL if |field| is out of range.
struct StrSplit : public sqlite::Function<StrSplit> {
  static constexpr char kName[] = "str_split";
  static constexpr int kArgCount = 3;

  static void Step(sqlite3_context* ctx, int argc, sqlite3_value** argv);
};

}  // namespace perfetto::trace_processor::string_functions

#endif  // SRC_TRACE_PROCESSOR_PLUGINS_STRING_FUNCTIONS_SQLITE3_STR_SPLIT_H_
