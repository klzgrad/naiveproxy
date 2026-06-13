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

#include "src/trace_processor/perfetto_sql/intrinsics/functions/art_heap_graph_functions.h"

#include <cstdint>
#include <cstring>
#include <string>

#include "perfetto/base/logging.h"
#include "perfetto/base/status.h"
#include "perfetto/ext/base/status_macros.h"
#include "src/trace_processor/perfetto_sql/engine/perfetto_sql_engine.h"
#include "src/trace_processor/sqlite/bindings/sqlite_function.h"
#include "src/trace_processor/sqlite/bindings/sqlite_result.h"
#include "src/trace_processor/sqlite/bindings/sqlite_type.h"
#include "src/trace_processor/sqlite/bindings/sqlite_value.h"
#include "src/trace_processor/sqlite/sqlite_utils.h"
#include "src/trace_processor/storage/trace_storage.h"
#include "src/trace_processor/types/trace_processor_context.h"
#include "src/trace_processor/util/json_serializer.h"

namespace perfetto::trace_processor {
namespace {

// Element type values matching art_hprof::FieldType.
constexpr uint8_t kFieldTypeBoolean = 4;
constexpr uint8_t kFieldTypeChar = 5;
constexpr uint8_t kFieldTypeFloat = 6;
constexpr uint8_t kFieldTypeDouble = 7;
constexpr uint8_t kFieldTypeByte = 8;
constexpr uint8_t kFieldTypeShort = 9;
constexpr uint8_t kFieldTypeInt = 10;
constexpr uint8_t kFieldTypeLong = 11;

size_t ElementSize(uint8_t type) {
  switch (type) {
    case kFieldTypeBoolean:
    case kFieldTypeByte:
      return 1;
    case kFieldTypeChar:
    case kFieldTypeShort:
      return 2;
    case kFieldTypeInt:
    case kFieldTypeFloat:
      return 4;
    case kFieldTypeLong:
    case kFieldTypeDouble:
      return 8;
    default:
      return 0;
  }
}

struct HeapGraphArray : public sqlite::Function<HeapGraphArray> {
  static constexpr char kName[] = "__intrinsic_heap_graph_array";
  static constexpr int kArgCount = 1;
  using UserData = TraceStorage;

  static void Step(sqlite3_context* ctx, int argc, sqlite3_value** argv) {
    PERFETTO_DCHECK(argc == 1);

    if (sqlite::value::Type(argv[0]) == sqlite::Type::kNull) {
      return sqlite::utils::ReturnNullFromFunction(ctx);
    }

    TraceStorage* storage = GetUserData(ctx);
    auto blob_id = static_cast<uint32_t>(sqlite::value::Int64(argv[0]));
    const auto& blobs = storage->hprof_array_blobs();

    if (blob_id >= blobs.size()) {
      return sqlite::utils::ReturnNullFromFunction(ctx);
    }

    const auto& blob = blobs[blob_id].data;
    sqlite::result::StaticBytes(ctx, blob.data(),
                                static_cast<int>(blob.length()));
  }
};

struct HeapGraphArrayJson : public sqlite::Function<HeapGraphArrayJson> {
  static constexpr char kName[] = "__intrinsic_heap_graph_array_json";
  static constexpr int kArgCount = 1;
  using UserData = TraceStorage;

  static void Step(sqlite3_context* ctx, int argc, sqlite3_value** argv) {
    PERFETTO_DCHECK(argc == 1);

    if (sqlite::value::Type(argv[0]) == sqlite::Type::kNull) {
      return sqlite::utils::ReturnNullFromFunction(ctx);
    }

    TraceStorage* storage = GetUserData(ctx);
    auto blob_id = static_cast<uint32_t>(sqlite::value::Int64(argv[0]));

    const auto& blobs = storage->hprof_array_blobs();
    if (blob_id >= blobs.size()) {
      return sqlite::utils::ReturnNullFromFunction(ctx);
    }
    const auto& array_blob = blobs[blob_id];
    uint8_t elem_type = array_blob.element_type;
    uint32_t count = array_blob.element_count;
    const uint8_t* data = array_blob.data.data();
    size_t data_len = array_blob.data.length();

    size_t elem_size = ElementSize(elem_type);
    if (elem_size == 0 || static_cast<size_t>(count) * elem_size > data_len) {
      return sqlite::utils::ReturnNullFromFunction(ctx);
    }

    json::JsonSerializer s;
    s.OpenArray();

    switch (elem_type) {
      case kFieldTypeBoolean:
        for (uint32_t i = 0; i < count; ++i) {
          s.BoolValue(data[i] != 0);
        }
        break;
      case kFieldTypeByte:
        for (uint32_t i = 0; i < count; ++i) {
          int8_t v;
          memcpy(&v, data + i, sizeof(v));
          s.NumberValue(v);
        }
        break;
      case kFieldTypeShort:
        for (uint32_t i = 0; i < count; ++i) {
          int16_t v;
          memcpy(&v, data + i * elem_size, sizeof(v));
          s.NumberValue(v);
        }
        break;
      case kFieldTypeInt:
        for (uint32_t i = 0; i < count; ++i) {
          int32_t v;
          memcpy(&v, data + i * elem_size, sizeof(v));
          s.NumberValue(v);
        }
        break;
      case kFieldTypeLong:
        for (uint32_t i = 0; i < count; ++i) {
          int64_t v;
          memcpy(&v, data + i * elem_size, sizeof(v));
          s.StringValue(std::to_string(v));
        }
        break;
      case kFieldTypeFloat:
        for (uint32_t i = 0; i < count; ++i) {
          float v;
          memcpy(&v, data + i * elem_size, sizeof(v));
          s.FloatValue(v);
        }
        break;
      case kFieldTypeDouble:
        for (uint32_t i = 0; i < count; ++i) {
          double v;
          memcpy(&v, data + i * elem_size, sizeof(v));
          s.DoubleValue(v);
        }
        break;
      case kFieldTypeChar:
        for (uint32_t i = 0; i < count; ++i) {
          uint16_t v;
          memcpy(&v, data + i * elem_size, sizeof(v));
          s.NumberValue(v);
        }
        break;
      default:
        break;
    }

    s.CloseArray();
    auto sv = s.GetStringView();
    sqlite::result::TransientString(ctx, sv.data(),
                                    static_cast<int>(sv.size()));
  }
};

}  // namespace

base::Status RegisterArtHeapGraphFunctions(PerfettoSqlEngine* engine,
                                           TraceProcessorContext* context) {
  RETURN_IF_ERROR(
      engine->RegisterFunction<HeapGraphArray>(context->storage.get()));
  return engine->RegisterFunction<HeapGraphArrayJson>(context->storage.get());
}

}  // namespace perfetto::trace_processor
