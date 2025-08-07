// Copyright (C) 2023 The Android Open Source Project
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//      http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include "src/trace_processor/perfetto_sql/intrinsics/functions/math.h"

#include <cmath>

#include "perfetto/base/logging.h"
#include "perfetto/base/status.h"
#include "perfetto/ext/base/status_macros.h"
#include "perfetto/trace_processor/basic_types.h"
#include "src/trace_processor/perfetto_sql/engine/perfetto_sql_engine.h"
#include "src/trace_processor/perfetto_sql/intrinsics/functions/sql_function.h"
#include "src/trace_processor/sqlite/sqlite_utils.h"

namespace perfetto::trace_processor {

namespace {

struct Ln : public SqlFunction {
  static base::Status Run(Context*,
                          size_t argc,
                          sqlite3_value** argv,
                          SqlValue& out,
                          Destructors&) {
    PERFETTO_CHECK(argc == 1);
    switch (sqlite3_value_numeric_type(argv[0])) {
      case SQLITE_INTEGER:
      case SQLITE_FLOAT: {
        double value = sqlite3_value_double(argv[0]);
        if (value > 0.0) {
          out = SqlValue::Double(std::log(value));
        }
        break;
      }
      case SqlValue::kNull:
      case SqlValue::kString:
      case SqlValue::kBytes:
        break;
    }

    return base::OkStatus();
  }
};

struct Exp : public SqlFunction {
  static base::Status Run(Context*,
                          size_t argc,
                          sqlite3_value** argv,
                          SqlValue& out,
                          Destructors&) {
    PERFETTO_CHECK(argc == 1);
    switch (sqlite3_value_numeric_type(argv[0])) {
      case SQLITE_INTEGER:
      case SQLITE_FLOAT:
        out = SqlValue::Double(std::exp(sqlite3_value_double(argv[0])));
        break;
      case SqlValue::kNull:
      case SqlValue::kString:
      case SqlValue::kBytes:
        break;
    }

    return base::OkStatus();
  }
};

struct Sqrt : public SqlFunction {
  static base::Status Run(Context*,
                          size_t argc,
                          sqlite3_value** argv,
                          SqlValue& out,
                          Destructors&) {
    PERFETTO_CHECK(argc == 1);
    switch (sqlite3_value_numeric_type(argv[0])) {
      case SQLITE_INTEGER:
      case SQLITE_FLOAT:
        out = SqlValue::Double(std::sqrt(sqlite3_value_double(argv[0])));
        break;
      case SqlValue::kNull:
      case SqlValue::kString:
      case SqlValue::kBytes:
        break;
    }

    return base::OkStatus();
  }
};

}  // namespace

base::Status RegisterMathFunctions(PerfettoSqlEngine& engine) {
  RETURN_IF_ERROR(engine.RegisterStaticFunction<Ln>("ln", 1, nullptr, true));
  RETURN_IF_ERROR(engine.RegisterStaticFunction<Exp>("exp", 1, nullptr, true));
  return engine.RegisterStaticFunction<Sqrt>("sqrt", 1, nullptr, true);
}

}  // namespace perfetto::trace_processor
