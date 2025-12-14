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

#include "src/trace_processor/perfetto_sql/intrinsics/functions/pprof_functions.h"

#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "perfetto/base/logging.h"
#include "perfetto/base/status.h"
#include "perfetto/ext/base/status_macros.h"
#include "perfetto/ext/base/status_or.h"
#include "perfetto/trace_processor/basic_types.h"
#include "protos/perfetto/trace_processor/stack.pbzero.h"
#include "src/trace_processor/perfetto_sql/engine/perfetto_sql_engine.h"
#include "src/trace_processor/sqlite/bindings/sqlite_result.h"
#include "src/trace_processor/sqlite/sqlite_utils.h"
#include "src/trace_processor/types/trace_processor_context.h"
#include "src/trace_processor/util/profile_builder.h"

// TODO(carlscab): We currently recreate the GProfileBuilder for every group. We
// should cache this somewhere maybe even have a helper table that stores all
// this data.

namespace perfetto::trace_processor {
namespace {

using protos::pbzero::Stack;

template <typename T>
std::unique_ptr<T> WrapUnique(T* ptr) {
  return std::unique_ptr<T>(ptr);
}

class AggregateContext {
 public:
  static base::StatusOr<std::unique_ptr<AggregateContext>>
  Create(TraceProcessorContext* tp_context, size_t argc, sqlite3_value** argv) {
    base::StatusOr<std::vector<GProfileBuilder::ValueType>> sample_types =
        GetSampleTypes(argc, argv);
    if (!sample_types.ok()) {
      return sample_types.status();
    }
    return WrapUnique(new AggregateContext(tp_context, sample_types.value()));
  }

  base::Status Step(size_t argc, sqlite3_value** argv) {
    RETURN_IF_ERROR(UpdateSampleValue(argc, argv));

    base::StatusOr<SqlValue> value = sqlite::utils::ExtractArgument(
        argc, argv, "stack", 0, SqlValue::kBytes);
    if (!value.ok()) {
      return value.status();
    }

    Stack::Decoder stack(static_cast<const uint8_t*>(value->bytes_value),
                         value->bytes_count);
    if (stack.bytes_left() != 0) {
      return sqlite::utils::ToInvalidArgumentError(
          "stack", 0, base::ErrStatus("failed to deserialize Stack proto"));
    }
    if (!builder_.AddSample(stack, sample_values_)) {
      return base::ErrStatus("Failed to add callstack");
    }
    return base::OkStatus();
  }

  void Final(sqlite3_context* ctx) {
    std::string profile_proto = builder_.Build();
    return sqlite::result::TransientBytes(
        ctx, profile_proto.data(), static_cast<int>(profile_proto.size()));
  }

 private:
  static base::StatusOr<std::vector<GProfileBuilder::ValueType>> GetSampleTypes(
      size_t argc,
      sqlite3_value** argv) {
    std::vector<GProfileBuilder::ValueType> sample_types;

    if (argc == 1) {
      sample_types.push_back({"samples", "count"});
    }

    for (size_t i = 1; i < argc; i += 3) {
      base::StatusOr<SqlValue> type = sqlite::utils::ExtractArgument(
          argc, argv, "sample_type", i, SqlValue::kString);
      if (!type.ok()) {
        return type.status();
      }

      base::StatusOr<SqlValue> units = sqlite::utils::ExtractArgument(
          argc, argv, "sample_units", i + 1, SqlValue::kString);
      if (!units.ok()) {
        return units.status();
      }

      sample_types.push_back({type->AsString(), units->AsString()});
    }
    return sample_types;
  }

  AggregateContext(TraceProcessorContext* tp_context,
                   const std::vector<GProfileBuilder::ValueType>& sample_types)
      : builder_(tp_context, sample_types) {
    sample_values_.resize(sample_types.size(), 1);
  }

  base::Status UpdateSampleValue(size_t argc, sqlite3_value** argv) {
    if (argc == 1) {
      PERFETTO_CHECK(sample_values_.size() == 1);
      return base::OkStatus();
    }

    PERFETTO_CHECK(argc == 1 + (sample_values_.size() * 3));
    for (size_t i = 0; i < sample_values_.size(); ++i) {
      base::StatusOr<SqlValue> value = sqlite::utils::ExtractArgument(
          argc, argv, "sample_value", 3 + i * 3, SqlValue::kLong);
      if (!value.ok()) {
        return value.status();
      }
      sample_values_[i] = value->AsLong();
    }

    return base::OkStatus();
  }

  GProfileBuilder builder_;
  std::vector<int64_t> sample_values_;
};

base::Status StepStatus(sqlite3_context* ctx,
                        size_t argc,
                        sqlite3_value** argv) {
  auto** agg_context_ptr = static_cast<AggregateContext**>(
      sqlite3_aggregate_context(ctx, sizeof(AggregateContext*)));
  if (!agg_context_ptr) {
    return base::ErrStatus("Failed to allocate aggregate context");
  }

  if (!*agg_context_ptr) {
    auto* tp_context =
        static_cast<TraceProcessorContext*>(sqlite3_user_data(ctx));
    base::StatusOr<std::unique_ptr<AggregateContext>> agg_context =
        AggregateContext::Create(tp_context, argc, argv);
    if (!agg_context.ok()) {
      return agg_context.status();
    }

    *agg_context_ptr = agg_context->release();
  }

  return (*agg_context_ptr)->Step(argc, argv);
}

struct ProfileBuilder {
  static constexpr char kName[] = "EXPERIMENTAL_PROFILE";
  static constexpr int kArgCount = -1;
  using UserData = TraceProcessorContext;

  static void Step(sqlite3_context* ctx, int argc, sqlite3_value** argv) {
    PERFETTO_CHECK(argc >= 0);

    base::Status status = StepStatus(ctx, static_cast<size_t>(argc), argv);
    if (!status.ok()) {
      sqlite::utils::SetError(ctx, kName, status);
    }
  }

  static void Final(sqlite3_context* ctx) {
    auto** agg_context_ptr =
        static_cast<AggregateContext**>(sqlite3_aggregate_context(ctx, 0));

    if (!agg_context_ptr) {
      return;
    }

    (*agg_context_ptr)->Final(ctx);

    delete (*agg_context_ptr);
  }
};

}  // namespace

base::Status PprofFunctions::Register(PerfettoSqlEngine& engine,
                                      TraceProcessorContext* context) {
  return engine.RegisterAggregateFunction<ProfileBuilder>(context);
}

}  // namespace perfetto::trace_processor
