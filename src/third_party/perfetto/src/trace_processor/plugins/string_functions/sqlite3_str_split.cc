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

#include "src/trace_processor/plugins/string_functions/sqlite3_str_split.h"

#include <cstring>

#include "perfetto/base/logging.h"
#include "src/trace_processor/sqlite/bindings/sqlite_result.h"
#include "src/trace_processor/sqlite/sqlite_utils.h"

namespace perfetto::trace_processor::string_functions {

namespace {
constexpr char kDelimiterError[] =
    "str_split: delimiter must be a non-empty string";
constexpr char kSplitFieldIndexError[] =
    "str_split: field number must be a non-negative integer";
}  // namespace

void StrSplit::Step(sqlite3_context* context,
                    int argc,
                    sqlite3_value** argv) {
  PERFETTO_DCHECK(argc == 3);
  if (sqlite3_value_type(argv[1]) != SQLITE_TEXT) {
    sqlite::result::Error(context, kDelimiterError);
    return;
  }
  const char* delimiter =
      reinterpret_cast<const char*>(sqlite3_value_text(argv[1]));
  const size_t delimiter_len = strlen(delimiter);
  if (delimiter_len == 0) {
    sqlite::result::Error(context, kDelimiterError);
    return;
  }
  if (sqlite3_value_type(argv[2]) != SQLITE_INTEGER) {
    sqlite::result::Error(context, kSplitFieldIndexError);
    return;
  }
  int fld = sqlite3_value_int(argv[2]);
  if (fld < 0) {
    sqlite::result::Error(context, kSplitFieldIndexError);
    return;
  }
  if (sqlite3_value_type(argv[0]) != SQLITE_TEXT) {
    sqlite::result::Null(context);
    return;
  }
  const char* in = reinterpret_cast<const char*>(sqlite3_value_text(argv[0]));
  const char* next;
  do {
    next = strstr(in, delimiter);
    if (fld == 0) {
      int size = next != nullptr ? static_cast<int>(next - in)
                                 : static_cast<int>(strlen(in));
      sqlite::result::RawString(context, in, size,
                                sqlite::utils::kSqliteTransient);
      return;
    }
    if (next == nullptr) {
      break;
    }
    in = next + delimiter_len;
    --fld;
  } while (fld >= 0);
  sqlite::result::Null(context);
}

}  // namespace perfetto::trace_processor::string_functions
