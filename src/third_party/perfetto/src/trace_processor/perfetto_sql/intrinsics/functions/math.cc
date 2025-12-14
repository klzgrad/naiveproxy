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
#include "src/trace_processor/perfetto_sql/engine/perfetto_sql_engine.h"
#include "src/trace_processor/sqlite/bindings/sqlite_function.h"
#include "src/trace_processor/sqlite/bindings/sqlite_result.h"
#include "src/trace_processor/sqlite/bindings/sqlite_type.h"
#include "src/trace_processor/sqlite/bindings/sqlite_value.h"
#include "src/trace_processor/sqlite/sqlite_utils.h"

namespace perfetto::trace_processor {

namespace {

struct Ln : public sqlite::Function<Ln> {
  static constexpr char kName[] = "ln";
  static constexpr int kArgCount = 1;

  static void Step(sqlite3_context* ctx, int argc, sqlite3_value** argv) {
    PERFETTO_DCHECK(argc == 1);
    switch (sqlite::value::NumericType(argv[0])) {
      case sqlite::Type::kInteger:
      case sqlite::Type::kFloat: {
        double value = sqlite::value::Double(argv[0]);
        if (value > 0.0) {
          return sqlite::result::Double(ctx, std::log(value));
        }
        break;
      }
      case sqlite::Type::kNull:
      case sqlite::Type::kText:
      case sqlite::Type::kBlob:
        break;
    }
    return sqlite::utils::ReturnNullFromFunction(ctx);
  }
};

struct Exp : public sqlite::Function<Exp> {
  static constexpr char kName[] = "exp";
  static constexpr int kArgCount = 1;

  static void Step(sqlite3_context* ctx, int argc, sqlite3_value** argv) {
    PERFETTO_DCHECK(argc == 1);
    switch (sqlite::value::NumericType(argv[0])) {
      case sqlite::Type::kInteger:
      case sqlite::Type::kFloat:
        return sqlite::result::Double(ctx,
                                      std::exp(sqlite::value::Double(argv[0])));
      case sqlite::Type::kNull:
      case sqlite::Type::kText:
      case sqlite::Type::kBlob:
        break;
    }
    return sqlite::utils::ReturnNullFromFunction(ctx);
  }
};

struct Sqrt : public sqlite::Function<Sqrt> {
  static constexpr char kName[] = "sqrt";
  static constexpr int kArgCount = 1;

  static void Step(sqlite3_context* ctx, int argc, sqlite3_value** argv) {
    PERFETTO_DCHECK(argc == 1);
    switch (sqlite::value::NumericType(argv[0])) {
      case sqlite::Type::kInteger:
      case sqlite::Type::kFloat:
        return sqlite::result::Double(
            ctx, std::sqrt(sqlite::value::Double(argv[0])));
      case sqlite::Type::kNull:
      case sqlite::Type::kText:
      case sqlite::Type::kBlob:
        break;
    }
    return sqlite::utils::ReturnNullFromFunction(ctx);
  }
};

}  // namespace

base::Status RegisterMathFunctions(PerfettoSqlEngine& engine) {
  RETURN_IF_ERROR(engine.RegisterFunction<Ln>(nullptr));
  RETURN_IF_ERROR(engine.RegisterFunction<Exp>(nullptr));
  return engine.RegisterFunction<Sqrt>(nullptr);
}

}  // namespace perfetto::trace_processor
