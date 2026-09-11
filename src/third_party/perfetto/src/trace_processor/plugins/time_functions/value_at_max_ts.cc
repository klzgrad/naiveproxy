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

#include "src/trace_processor/plugins/time_functions/value_at_max_ts.h"

#include <sqlite3.h>
#include <cstdint>
#include <limits>

#include "perfetto/base/compiler.h"
#include "perfetto/base/logging.h"
#include "src/trace_processor/sqlite/bindings/sqlite_result.h"

namespace perfetto::trace_processor {

void ValueAtMaxTs::Step(sqlite3_context* ctx, int, sqlite3_value** argv) {
  sqlite3_value* ts = argv[0];
  sqlite3_value* value = argv[1];

  // Note that sqlite3_aggregate_context zeros the memory for us so all the
  // variables of the struct should be zero.
  auto* fn_ctx = reinterpret_cast<Context*>(
      sqlite3_aggregate_context(ctx, sizeof(Context)));

  // For performance reasons, we only do the check for the type of ts and
  // value on the first call of the function.
  if (PERFETTO_UNLIKELY(!fn_ctx->initialized)) {
    if (sqlite3_value_type(ts) != SQLITE_INTEGER) {
      return sqlite::result::Error(
          ctx, "VALUE_AT_MAX_TS: ts passed was not an integer");
    }

    fn_ctx->value_type = sqlite3_value_type(value);
    if (fn_ctx->value_type != SQLITE_INTEGER &&
        fn_ctx->value_type != SQLITE_FLOAT) {
      return sqlite::result::Error(
          ctx, "VALUE_AT_MAX_TS: value passed was not an integer or float");
    }

    fn_ctx->max_ts = std::numeric_limits<int64_t>::min();
    fn_ctx->initialized = true;
  }

  // On dcheck builds however, we check every passed ts and value.
#if PERFETTO_DCHECK_IS_ON()
  if (sqlite3_value_type(ts) != SQLITE_INTEGER) {
    return sqlite::result::Error(
        ctx, "VALUE_AT_MAX_TS: ts passed was not an integer");
  }
  if (sqlite3_value_type(value) != fn_ctx->value_type) {
    return sqlite::result::Error(ctx,
                                 "VALUE_AT_MAX_TS: value type is inconsistent");
  }
#endif

  int64_t ts_int = sqlite3_value_int64(ts);
  if (PERFETTO_LIKELY(fn_ctx->max_ts <= ts_int)) {
    fn_ctx->max_ts = ts_int;

    if (fn_ctx->value_type == SQLITE_INTEGER) {
      fn_ctx->int_value_at_max_ts = sqlite3_value_int64(value);
    } else {
      fn_ctx->double_value_at_max_ts = sqlite3_value_double(value);
    }
  }
}

void ValueAtMaxTs::Final(sqlite3_context* ctx) {
  auto* fn_ctx = static_cast<Context*>(sqlite3_aggregate_context(ctx, 0));
  if (!fn_ctx) {
    sqlite::result::Null(ctx);
    return;
  }
  if (fn_ctx->value_type == SQLITE_INTEGER) {
    sqlite::result::Long(ctx, fn_ctx->int_value_at_max_ts);
  } else {
    sqlite::result::Double(ctx, fn_ctx->double_value_at_max_ts);
  }
}

}  // namespace perfetto::trace_processor
