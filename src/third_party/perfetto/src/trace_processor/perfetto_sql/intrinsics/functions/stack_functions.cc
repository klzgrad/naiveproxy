/*
 * Copyright (C) 2023 The Android Open Source Project
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

#include "src/trace_processor/perfetto_sql/intrinsics/functions/stack_functions.h"

#include <stdlib.h>
#include <cstdint>
#include <cstring>
#include <deque>
#include <iterator>
#include <optional>
#include <type_traits>
#include <vector>

#include "perfetto/base/logging.h"
#include "perfetto/base/status.h"
#include "perfetto/ext/base/status_macros.h"
#include "perfetto/ext/base/utils.h"
#include "perfetto/protozero/scattered_heap_buffer.h"
#include "perfetto/trace_processor/basic_types.h"
#include "perfetto/trace_processor/status.h"
#include "protos/perfetto/trace_processor/stack.pbzero.h"
#include "src/trace_processor/perfetto_sql/engine/perfetto_sql_engine.h"
#include "src/trace_processor/perfetto_sql/intrinsics/functions/sql_function.h"
#include "src/trace_processor/sqlite/bindings/sqlite_function.h"
#include "src/trace_processor/sqlite/bindings/sqlite_result.h"
#include "src/trace_processor/sqlite/bindings/sqlite_type.h"
#include "src/trace_processor/sqlite/bindings/sqlite_value.h"
#include "src/trace_processor/sqlite/sqlite_utils.h"
#include "src/trace_processor/storage/trace_storage.h"
#include "src/trace_processor/types/trace_processor_context.h"

namespace perfetto {
namespace trace_processor {
namespace {

using protos::pbzero::Stack;

void SetBytesResult(sqlite3_context* ctx, const std::vector<uint8_t>& src) {
  return sqlite::result::TransientBytes(ctx, src.data(),
                                        static_cast<int>(src.size()));
}

// CAT_STACKS(root BLOB/STRING, level_1 BLOB/STRING, …, leaf BLOB/STRING)
// Creates a Stack by concatenating other Stacks. Also accepts strings for which
// it generates a fake Frame
struct CatStacksFunction : public sqlite::Function<CatStacksFunction> {
  static constexpr char kName[] = "CAT_STACKS";
  static constexpr int kArgCount = -1;

  static void Step(sqlite3_context* ctx, int argc, sqlite3_value** argv) {
    PERFETTO_DCHECK(argc >= 0);
    protozero::HeapBuffered<Stack> stack;

    // Note, this SQL function expects the root frame to be the first argument.
    // Stack expects the opposite, thus iterates the args in reverse order.
    for (int i = argc; i > 0; --i) {
      int arg_index = i - 1;
      switch (sqlite::value::Type(argv[arg_index])) {
        case sqlite::Type::kBlob: {
          const void* blob = sqlite::value::Blob(argv[arg_index]);
          int size = sqlite::value::Bytes(argv[arg_index]);
          stack->AppendRawProtoBytes(blob, static_cast<size_t>(size));
          break;
        }
        case sqlite::Type::kText: {
          stack->add_entries()->set_name(sqlite::value::Text(argv[arg_index]));
          break;
        }
        case sqlite::Type::kNull:
          break;
        case sqlite::Type::kInteger:
        case sqlite::Type::kFloat:
          return sqlite::utils::SetError(
              ctx, base::ErrStatus(
                       "CAT_STACKS: entry %d must be BLOB, STRING, or NULL",
                       arg_index));
      }
    }

    return SetBytesResult(ctx, stack.SerializeAsArray());
  }
};

// STACK_FROM_STACK_PROFILE_CALLSITE(callsite_id LONG, [annotate BOOLEAN])
// Creates a stack by taking a callsite_id (reference to the
// stack_profile_callsite table) and generating a list of frames (by walking the
// stack_profile_callsite table)
// Optionally annotates frames (annotate param has a default value of false)
//
// Important: Annotations might interfere with certain aggregations, as we
// will could have a frame that is annotated with different annotations. That
// will lead to multiple functions being generated (same name, line etc, but
// different annotation).
struct StackFromStackProfileCallsiteFunction
    : public sqlite::Function<StackFromStackProfileCallsiteFunction> {
  static constexpr char kName[] = "STACK_FROM_STACK_PROFILE_CALLSITE";
  static constexpr int kArgCount = -1;

  using UserData = TraceStorage;

  static void Step(sqlite3_context* ctx, int argc, sqlite3_value** argv) {
    TraceStorage* storage = GetUserData(ctx);
    PERFETTO_DCHECK(argc == 1 || argc == 2);

    int64_t callsite_id_long = 0;
    switch (sqlite::value::Type(argv[0])) {
      case sqlite::Type::kInteger:
        callsite_id_long = sqlite::value::Int64(argv[0]);
        break;
      case sqlite::Type::kNull:
        return sqlite::utils::ReturnNullFromFunction(ctx);
      case sqlite::Type::kFloat:
      case sqlite::Type::kText:
      case sqlite::Type::kBlob:
        return sqlite::utils::SetError(
            ctx,
            "STACK_FROM_STACK_PROFILE_CALLSITE: callsite_id must be integer");
    }

    if (callsite_id_long > std::numeric_limits<uint32_t>::max() ||
        callsite_id_long < 0 ||
        !storage->stack_profile_callsite_table()
             .FindById(tables::StackProfileCallsiteTable::Id(
                 static_cast<uint32_t>(callsite_id_long)))
             .has_value()) {
      return sqlite::utils::SetError(
          ctx, base::ErrStatus("STACK_FROM_STACK_PROFILE_CALLSITE: callsite_id "
                               "does not exist: %" PRId64,
                               callsite_id_long));
    }

    uint32_t callsite_id = static_cast<uint32_t>(callsite_id_long);

    bool annotate = false;
    if (argc == 2) {
      switch (sqlite::value::Type(argv[1])) {
        case sqlite::Type::kInteger:
          // true = 1 and false = 0 in SQL
          annotate = (sqlite::value::Int64(argv[1]) != 0);
          break;
        case sqlite::Type::kNull:
          return sqlite::utils::ReturnNullFromFunction(ctx);
        case sqlite::Type::kFloat:
        case sqlite::Type::kText:
        case sqlite::Type::kBlob:
          return sqlite::utils::SetError(
              ctx,
              "STACK_FROM_STACK_PROFILE_CALLSITE: annotate must be integer");
      }
    }

    protozero::HeapBuffered<Stack> stack;
    if (annotate) {
      stack->add_entries()->set_annotated_callsite_id(callsite_id);
    } else {
      stack->add_entries()->set_callsite_id(callsite_id);
    }
    return SetBytesResult(ctx, stack.SerializeAsArray());
  }
};

// STACK_FROM_STACK_PROFILE_FRAME(frame_id LONG)
// Creates a stack with just the frame referenced by frame_id (reference to the
// stack_profile_frame table)
struct StackFromStackProfileFrameFunction
    : public sqlite::Function<StackFromStackProfileFrameFunction> {
  static constexpr char kName[] = "STACK_FROM_STACK_PROFILE_FRAME";
  static constexpr int kArgCount = 1;

  using UserData = TraceStorage;

  static void Step(sqlite3_context* ctx, int argc, sqlite3_value** argv) {
    TraceStorage* storage = GetUserData(ctx);
    PERFETTO_DCHECK(argc == 1);

    int64_t frame_id_long = 0;
    switch (sqlite::value::Type(argv[0])) {
      case sqlite::Type::kInteger:
        frame_id_long = sqlite::value::Int64(argv[0]);
        break;
      case sqlite::Type::kNull:
        return sqlite::utils::ReturnNullFromFunction(ctx);
      case sqlite::Type::kFloat:
      case sqlite::Type::kText:
      case sqlite::Type::kBlob:
        return sqlite::utils::SetError(
            ctx, "STACK_FROM_STACK_PROFILE_FRAME: frame_id must be integer");
    }

    if (frame_id_long > std::numeric_limits<uint32_t>::max() ||
        frame_id_long < 0 ||
        !storage->stack_profile_frame_table()
             .FindById(tables::StackProfileFrameTable::Id(
                 static_cast<uint32_t>(frame_id_long)))
             .has_value()) {
      return sqlite::utils::SetError(
          ctx, base::ErrStatus("STACK_FROM_STACK_PROFILE_FRAME: frame_id does "
                               "not exist: %" PRId64,
                               frame_id_long));
    }

    uint32_t frame_id = static_cast<uint32_t>(frame_id_long);
    protozero::HeapBuffered<Stack> stack;
    stack->add_entries()->set_frame_id(frame_id);
    return SetBytesResult(ctx, stack.SerializeAsArray());
  }
};

}  // namespace

base::Status RegisterStackFunctions(PerfettoSqlEngine* engine,
                                    TraceProcessorContext* context) {
  RETURN_IF_ERROR(engine->RegisterFunction<CatStacksFunction>(nullptr));
  RETURN_IF_ERROR(engine->RegisterFunction<StackFromStackProfileFrameFunction>(
      context->storage.get()));
  return engine->RegisterFunction<StackFromStackProfileCallsiteFunction>(
      context->storage.get());
}

}  // namespace trace_processor
}  // namespace perfetto
