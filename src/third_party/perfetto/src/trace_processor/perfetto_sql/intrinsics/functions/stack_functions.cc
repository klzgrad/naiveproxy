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
#include "perfetto/protozero/scattered_heap_buffer.h"
#include "perfetto/trace_processor/basic_types.h"
#include "perfetto/trace_processor/status.h"
#include "protos/perfetto/trace_processor/stack.pbzero.h"
#include "src/trace_processor/perfetto_sql/engine/perfetto_sql_engine.h"
#include "src/trace_processor/perfetto_sql/intrinsics/functions/sql_function.h"
#include "src/trace_processor/sqlite/sqlite_utils.h"
#include "src/trace_processor/storage/trace_storage.h"
#include "src/trace_processor/types/trace_processor_context.h"

namespace perfetto {
namespace trace_processor {
namespace {

using protos::pbzero::Stack;

base::Status SetBytesOutputValue(const std::vector<uint8_t>& src,
                                 SqlValue& out,
                                 SqlFunction::Destructors& destructors) {
  void* dest = malloc(src.size());
  if (dest == nullptr) {
    return base::ErrStatus("Out of memory");
  }
  memcpy(dest, src.data(), src.size());
  out = SqlValue::Bytes(dest, src.size());
  destructors.bytes_destructor = free;
  return base::OkStatus();
}

// CAT_STACKS(root BLOB/STRING, level_1 BLOB/STRING, …, leaf BLOB/STRING)
// Creates a Stack by concatenating other Stacks. Also accepts strings for which
// it generates a fake Frame
struct CatStacksFunction : public SqlFunction {
  static constexpr char kFunctionName[] = "CAT_STACKS";
  using Context = void;

  static base::Status Run(void* cxt,
                          size_t argc,
                          sqlite3_value** argv,
                          SqlValue& out,
                          Destructors& destructors) {
    base::Status status = RunImpl(cxt, argc, argv, out, destructors);
    if (!status.ok()) {
      return base::ErrStatus("%s: %s", kFunctionName, status.message().c_str());
    }
    return status;
  }

  static base::Status RunImpl(void*,
                              size_t argc,
                              sqlite3_value** argv,
                              SqlValue& out,
                              Destructors& destructors) {
    protozero::HeapBuffered<Stack> stack;

    // Note, this SQL function expects the root frame to be the first argument.
    // Stack expects the opposite, thus iterates the args in reverse order.
    for (size_t i = argc; i > 0; --i) {
      size_t arg_index = i - 1;
      SqlValue value = sqlite::utils::SqliteValueToSqlValue(argv[arg_index]);
      switch (value.type) {
        case SqlValue::kBytes: {
          stack->AppendRawProtoBytes(value.bytes_value, value.bytes_count);
          break;
        }
        case SqlValue::kString: {
          stack->add_entries()->set_name(value.AsString());
          break;
        }
        case SqlValue::kNull:
          break;
        case SqlValue::kLong:
        case SqlValue::kDouble:
          return sqlite::utils::InvalidArgumentTypeError(
              "entry", arg_index, value.type, SqlValue::kBytes,
              SqlValue::kString, SqlValue::kNull);
      }
    }

    return SetBytesOutputValue(stack.SerializeAsArray(), out, destructors);
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
struct StackFromStackProfileCallsiteFunction : public SqlFunction {
  static constexpr char kFunctionName[] = "STACK_FROM_STACK_PROFILE_CALLSITE";
  using Context = TraceStorage;

  static base::Status Run(TraceStorage* storage,
                          size_t argc,
                          sqlite3_value** argv,
                          SqlValue& out,
                          Destructors& destructors) {
    base::Status status = RunImpl(storage, argc, argv, out, destructors);
    if (!status.ok()) {
      return base::ErrStatus("%s: %s", kFunctionName, status.message().c_str());
    }
    return status;
  }

  static base::Status RunImpl(TraceStorage* storage,
                              size_t argc,
                              sqlite3_value** argv,
                              SqlValue& out,
                              Destructors& destructors) {
    if (argc != 1 && argc != 2) {
      return base::ErrStatus(
          "%s; Invalid number of arguments: expected 1 or 2, actual %zu",
          kFunctionName, argc);
    }

    base::StatusOr<SqlValue> value = sqlite::utils::ExtractArgument(
        argc, argv, "callsite_id", 0, SqlValue::kNull, SqlValue::kLong);
    if (!value.ok()) {
      return value.status();
    }
    if (value->is_null()) {
      return base::OkStatus();
    }

    if (value->AsLong() > std::numeric_limits<uint32_t>::max() ||
        !storage->stack_profile_callsite_table()
             .FindById(tables::StackProfileCallsiteTable::Id(
                 static_cast<uint32_t>(value->AsLong())))
             .has_value()) {
      return sqlite::utils::ToInvalidArgumentError(
          "callsite_id", 0,
          base::ErrStatus("callsite_id does not exist: %" PRId64,
                          value->AsLong()));
    }

    uint32_t callsite_id = static_cast<uint32_t>(value->AsLong());

    bool annotate = false;
    if (argc == 2) {
      value = sqlite::utils::ExtractArgument(argc, argv, "annotate", 1,
                                             SqlValue::Type::kLong);
      if (!value.ok()) {
        return value.status();
      }
      // true = 1 and false = 0 in SQL
      annotate = (value->AsLong() != 0);
    }

    protozero::HeapBuffered<Stack> stack;
    if (annotate) {
      stack->add_entries()->set_annotated_callsite_id(callsite_id);
    } else {
      stack->add_entries()->set_callsite_id(callsite_id);
    }
    return SetBytesOutputValue(stack.SerializeAsArray(), out, destructors);
  }
};

// STACK_FROM_STACK_PROFILE_FRAME(frame_id LONG)
// Creates a stack with just the frame referenced by frame_id (reference to the
// stack_profile_frame table)
struct StackFromStackProfileFrameFunction : public SqlFunction {
  static constexpr char kFunctionName[] = "STACK_FROM_STACK_PROFILE_FRAME";
  using Context = TraceStorage;

  static base::Status Run(TraceStorage* storage,
                          size_t argc,
                          sqlite3_value** argv,
                          SqlValue& out,
                          Destructors& destructors) {
    base::Status status = RunImpl(storage, argc, argv, out, destructors);
    if (!status.ok()) {
      return base::ErrStatus("%s: %s", kFunctionName, status.message().c_str());
    }
    return status;
  }

  static base::Status RunImpl(TraceStorage* storage,
                              size_t argc,
                              sqlite3_value** argv,
                              SqlValue& out,
                              Destructors& destructors) {
    base::StatusOr<SqlValue> value = sqlite::utils::ExtractArgument(
        argc, argv, "frame_id", 0, SqlValue::kNull, SqlValue::kLong);

    if (!value.ok()) {
      return value.status();
    }

    if (value->is_null()) {
      return base::OkStatus();
    }

    if (value->AsLong() > std::numeric_limits<uint32_t>::max() ||
        !storage->stack_profile_frame_table()
             .FindById(tables::StackProfileFrameTable::Id(
                 static_cast<uint32_t>(value->AsLong())))
             .has_value()) {
      return base::ErrStatus("%s; frame_id does not exist: %" PRId64,
                             kFunctionName, value->AsLong());
    }

    uint32_t frame_id = static_cast<uint32_t>(value->AsLong());
    protozero::HeapBuffered<Stack> stack;
    stack->add_entries()->set_frame_id(frame_id);
    return SetBytesOutputValue(stack.SerializeAsArray(), out, destructors);
  }
};

}  // namespace

base::Status RegisterStackFunctions(PerfettoSqlEngine* engine,
                                    TraceProcessorContext* context) {
  RETURN_IF_ERROR(engine->RegisterStaticFunction<CatStacksFunction>(
      CatStacksFunction::kFunctionName, -1, context->storage.get()));
  RETURN_IF_ERROR(
      engine->RegisterStaticFunction<StackFromStackProfileFrameFunction>(
          StackFromStackProfileFrameFunction::kFunctionName, 1,
          context->storage.get()));
  return engine->RegisterStaticFunction<StackFromStackProfileCallsiteFunction>(
      StackFromStackProfileCallsiteFunction::kFunctionName, -1,
      context->storage.get());
}

}  // namespace trace_processor
}  // namespace perfetto
