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

#ifndef SRC_TRACE_PROCESSOR_SQLITE_SQLITE_TAGGED_ARGS_H_
#define SRC_TRACE_PROCESSOR_SQLITE_SQLITE_TAGGED_ARGS_H_

#include <sqlite3.h>  // IWYU pragma: export

#include <cstdint>
#include <cstring>
#include <functional>
#include <initializer_list>
#include <string>

#include "perfetto/base/status.h"
#include "perfetto/ext/base/status_macros.h"
#include "perfetto/ext/base/status_or.h"
#include "src/trace_processor/sqlite/bindings/sqlite_type.h"
#include "src/trace_processor/sqlite/bindings/sqlite_value.h"

namespace perfetto::trace_processor::sqlite {

// Support for functions taking a flat "tagged token list" argument
// convention: a tag string token followed by a fixed number of value tokens,
// with tags repeatable to express lists of tuples, e.g.
//
//   my_fn('view', 'PIVOT', 'filter', 'SHOW_STACK', 'foo.*')
//
// This is the standard convention for passing structured configuration to
// intrinsic functions which back (or will back) PerfettoSQL operators: it
// keeps generated SQL readable and hand-writable while allowing arbitrary
// repeated tuples. Functions using it should parse their arguments eagerly
// and report precise errors so mistakes surface at the call site.

// One accepted tag: its name, the number of value tokens following it and a
// handler invoked with a pointer to those values.
struct TaggedArgSpec {
  const char* tag;
  uint32_t arity;
  std::function<base::Status(sqlite3_value**)> handler;
};

// Reads a value token which must be non-null text. |context| is used for
// error messages and should identify the function and tag.
inline base::StatusOr<std::string> TaggedArgText(const char* context,
                                                 sqlite3_value* value) {
  if (sqlite::value::Type(value) != sqlite::Type::kText) {
    return base::ErrStatus("%s: expected a string value", context);
  }
  return std::string(sqlite::value::Text(value));
}

// Walks |argv| dispatching each tag to the matching spec. Returns an error
// on unknown tags, missing values or handler failure.
inline base::Status ParseTaggedArgs(
    const char* fn_name,
    int argc,
    sqlite3_value** argv,
    std::initializer_list<TaggedArgSpec> specs) {
  for (int i = 0; i < argc;) {
    if (sqlite::value::Type(argv[i]) != sqlite::Type::kText) {
      return base::ErrStatus("%s: expected a tag string at argument %d",
                             fn_name, i);
    }
    const char* tag = sqlite::value::Text(argv[i]);
    const TaggedArgSpec* match = nullptr;
    for (const TaggedArgSpec& spec : specs) {
      if (strcmp(spec.tag, tag) == 0) {
        match = &spec;
        break;
      }
    }
    if (!match) {
      return base::ErrStatus("%s: unknown tag '%s' at argument %d", fn_name,
                             tag, i);
    }
    if (i + 1 + static_cast<int>(match->arity) > argc) {
      return base::ErrStatus("%s: tag '%s' expects %u value(s)", fn_name, tag,
                             match->arity);
    }
    RETURN_IF_ERROR(match->handler(argv + i + 1));
    i += 1 + static_cast<int>(match->arity);
  }
  return base::OkStatus();
}

}  // namespace perfetto::trace_processor::sqlite

#endif  // SRC_TRACE_PROCESSOR_SQLITE_SQLITE_TAGGED_ARGS_H_
