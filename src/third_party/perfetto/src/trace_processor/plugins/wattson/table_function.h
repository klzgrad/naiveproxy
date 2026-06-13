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

#ifndef SRC_TRACE_PROCESSOR_PLUGINS_WATTSON_TABLE_FUNCTION_H_
#define SRC_TRACE_PROCESSOR_PLUGINS_WATTSON_TABLE_FUNCTION_H_

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>

#include "perfetto/ext/base/status_or.h"
#include "src/trace_processor/containers/string_pool.h"
#include "src/trace_processor/core/dataframe/dataframe.h"
#include "src/trace_processor/core/dataframe/specs.h"
#include "src/trace_processor/perfetto_sql/intrinsics/table_functions/static_table_function.h"

namespace perfetto::trace_processor::wattson {

// Returns rows for a single wattson curve type from a compressed blob
// produced by `gen_wattson_curves.py`. The caller hands in the
// `DataframeSpec` describing the column shape; the blob's column-oriented
// binary layout must follow that same order with `Int64`, `Double` and
// `String` columns encoded as `int64`, `float64` and `uint32` (string-table
// indices) respectively.
//
// The blob is decompressed and parsed every time `Cursor::Run` is invoked;
// callers that need a stable view materialize it once via
// `CREATE PERFETTO TABLE` and query that.
class WattsonCurvesTableFunction : public StaticTableFunction {
 public:
  WattsonCurvesTableFunction(StringPool* pool,
                             std::string table_name,
                             dataframe::DataframeSpec spec,
                             const uint8_t* compressed_blob,
                             size_t compressed_blob_size);
  ~WattsonCurvesTableFunction() override;

  std::unique_ptr<Cursor> MakeCursor() override;
  dataframe::DataframeSpec CreateSpec() override;
  std::string TableName() override;
  uint32_t GetArgumentCount() const override;

  // Decompresses, parses and returns a fresh dataframe. Exposed so the
  // cursor can own the result.
  base::StatusOr<dataframe::Dataframe> BuildDataframe() const;

 private:
  class Cursor;

  StringPool* pool_;
  std::string table_name_;
  dataframe::DataframeSpec spec_;
  const uint8_t* compressed_blob_;
  size_t compressed_blob_size_;
};

}  // namespace perfetto::trace_processor::wattson

#endif  // SRC_TRACE_PROCESSOR_PLUGINS_WATTSON_TABLE_FUNCTION_H_
