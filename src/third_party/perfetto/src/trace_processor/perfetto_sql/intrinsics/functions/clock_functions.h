/*
 * Copyright (C) 2022 The Android Open Source Project
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

#ifndef SRC_TRACE_PROCESSOR_PERFETTO_SQL_INTRINSICS_FUNCTIONS_CLOCK_FUNCTIONS_H_
#define SRC_TRACE_PROCESSOR_PERFETTO_SQL_INTRINSICS_FUNCTIONS_CLOCK_FUNCTIONS_H_

#include <cinttypes>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <string>

#include "perfetto/base/logging.h"
#include "perfetto/ext/base/status_or.h"
#include "perfetto/ext/base/string_utils.h"
#include "src/trace_processor/importers/common/clock_converter.h"
#include "src/trace_processor/sqlite/bindings/sqlite_function.h"
#include "src/trace_processor/sqlite/bindings/sqlite_result.h"
#include "src/trace_processor/sqlite/bindings/sqlite_type.h"
#include "src/trace_processor/sqlite/bindings/sqlite_value.h"
#include "src/trace_processor/sqlite/sqlite_utils.h"

namespace perfetto::trace_processor {

struct AbsTimeStr : public sqlite::Function<AbsTimeStr> {
  static constexpr char kName[] = "abs_time_str";
  static constexpr int kArgCount = 1;

  using UserData = ClockConverter;
  static void Step(sqlite3_context* ctx, int argc, sqlite3_value** argv);
};

void AbsTimeStr::Step(sqlite3_context* ctx, int argc, sqlite3_value** argv) {
  PERFETTO_DCHECK(argc == 1);

  switch (sqlite::value::Type(argv[0])) {
    case sqlite::Type::kNull:
      return sqlite::utils::ReturnNullFromFunction(ctx);
    case sqlite::Type::kInteger: {
      auto* tracker = GetUserData(ctx);
      int64_t ts = sqlite::value::Int64(argv[0]);
      base::StatusOr<std::string> iso8601 = tracker->ToAbsTime(ts);
      if (!iso8601.ok()) {
        // We are returning NULL, because one bad timestamp shouldn't stop
        // the query.
        return sqlite::utils::ReturnNullFromFunction(ctx);
      }

      return sqlite::result::TransientString(ctx, iso8601->c_str(),
                                             static_cast<int>(iso8601->size()));
    }
    case sqlite::Type::kFloat:
    case sqlite::Type::kText:
    case sqlite::Type::kBlob:
      return sqlite::utils::SetError(
          ctx, "ABS_TIME_STR: first argument should be timestamp");
  }
}

struct ToMonotonic : public sqlite::Function<ToMonotonic> {
  static constexpr char kName[] = "to_monotonic";
  static constexpr int kArgCount = 1;

  using UserData = ClockConverter;
  static void Step(sqlite3_context* ctx, int argc, sqlite3_value** argv);
};

void ToMonotonic::Step(sqlite3_context* ctx, int argc, sqlite3_value** argv) {
  PERFETTO_DCHECK(argc == 1);

  switch (sqlite::value::Type(argv[0])) {
    case sqlite::Type::kNull:
      return sqlite::utils::ReturnNullFromFunction(ctx);
    case sqlite::Type::kInteger: {
      auto* converter = GetUserData(ctx);
      int64_t ts = sqlite::value::Int64(argv[0]);
      base::StatusOr<int64_t> monotonic = converter->ToMonotonic(ts);

      if (!monotonic.ok()) {
        // We are returning NULL, because one bad timestamp shouldn't stop
        // the query.
        return sqlite::utils::ReturnNullFromFunction(ctx);
      }

      return sqlite::result::Long(ctx, *monotonic);
    }
    case sqlite::Type::kFloat:
    case sqlite::Type::kText:
    case sqlite::Type::kBlob:
      return sqlite::utils::SetError(
          ctx, "TO_MONOTONIC: first argument should be timestamp");
  }
}

struct ToRealtime : public sqlite::Function<ToRealtime> {
  static constexpr char kName[] = "to_realtime";
  static constexpr int kArgCount = 1;

  using UserData = ClockConverter;
  static void Step(sqlite3_context* ctx, int argc, sqlite3_value** argv);
};

void ToRealtime::Step(sqlite3_context* ctx, int argc, sqlite3_value** argv) {
  PERFETTO_DCHECK(argc == 1);

  switch (sqlite::value::Type(argv[0])) {
    case sqlite::Type::kNull:
      return sqlite::utils::ReturnNullFromFunction(ctx);
    case sqlite::Type::kInteger: {
      auto* converter = GetUserData(ctx);
      int64_t ts = sqlite::value::Int64(argv[0]);
      base::StatusOr<int64_t> realtime = converter->ToRealtime(ts);

      if (!realtime.ok()) {
        // We are returning NULL, because one bad timestamp shouldn't stop
        // the query.
        return sqlite::utils::ReturnNullFromFunction(ctx);
      }

      return sqlite::result::Long(ctx, *realtime);
    }
    case sqlite::Type::kFloat:
    case sqlite::Type::kText:
    case sqlite::Type::kBlob:
      return sqlite::utils::SetError(
          ctx, "TO_REALTIME: first argument should be timestamp");
  }
}

struct ToTimecode : public sqlite::Function<ToTimecode> {
  static constexpr char kName[] = "to_timecode";
  static constexpr int kArgCount = 1;

  static void Step(sqlite3_context* ctx, int argc, sqlite3_value** argv);
};

void ToTimecode::Step(sqlite3_context* ctx, int argc, sqlite3_value** argv) {
  PERFETTO_DCHECK(argc == 1);

  switch (sqlite::value::Type(argv[0])) {
    case sqlite::Type::kNull:
      return sqlite::utils::ReturnNullFromFunction(ctx);
    case sqlite::Type::kInteger: {
      int64_t ns = sqlite::value::Int64(argv[0]);

      int64_t us = ns / 1000;
      ns = ns % 1000;

      int64_t ms = us / 1000;
      us = us % 1000;

      int64_t ss = ms / 1000;
      ms = ms % 1000;

      int64_t mm = ss / 60;
      ss = ss % 60;

      int64_t hh = mm / 60;
      mm = mm % 60;

      base::StackString<64> buf("%02" PRId64 ":%02" PRId64 ":%02" PRId64
                                " %03" PRId64 " %03" PRId64 " %03" PRId64,
                                hh, mm, ss, ms, us, ns);

      return sqlite::result::TransientString(ctx, buf.c_str(),
                                             static_cast<int>(buf.len()));
    }
    case sqlite::Type::kFloat:
    case sqlite::Type::kText:
    case sqlite::Type::kBlob:
      return sqlite::utils::SetError(
          ctx, "TO_TIMECODE: first argument should be timestamp");
  }
}

}  // namespace perfetto::trace_processor

#endif  // SRC_TRACE_PROCESSOR_PERFETTO_SQL_INTRINSICS_FUNCTIONS_CLOCK_FUNCTIONS_H_
