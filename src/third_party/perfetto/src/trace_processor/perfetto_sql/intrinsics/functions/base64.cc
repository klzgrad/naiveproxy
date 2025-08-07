// Copyright (C) 2024 The Android Open Source Project
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

#include "src/trace_processor/perfetto_sql/intrinsics/functions/base64.h"

#include <cstddef>
#include <cstdlib>
#include <cstring>

#include "perfetto/base/status.h"
#include "perfetto/ext/base/base64.h"
#include "perfetto/trace_processor/basic_types.h"
#include "src/trace_processor/perfetto_sql/engine/perfetto_sql_engine.h"
#include "src/trace_processor/perfetto_sql/intrinsics/functions/sql_function.h"
#include "src/trace_processor/sqlite/sqlite_utils.h"

namespace perfetto::trace_processor {

namespace {

struct Base64Decode : public SqlFunction {
  static base::Status Run(Context*,
                          size_t argc,
                          sqlite3_value** argv,
                          SqlValue& out,
                          Destructors& destructors) {
    if (argc != 1) {
      return base::ErrStatus("BASE64: expected one arg but got %zu", argc);
    }

    auto in = sqlite::utils::SqliteValueToSqlValue(argv[0]);

    const char* src = nullptr;
    size_t src_size = 0;
    switch (in.type) {
      case SqlValue::kNull:
        return base::OkStatus();
      case SqlValue::kLong:
      case SqlValue::kDouble:
        return base::ErrStatus("BASE64: argument must be string or blob");
      case SqlValue::kString:
        src = in.AsString();
        src_size = strlen(src);
        break;
      case SqlValue::kBytes:
        src = reinterpret_cast<const char*>(in.AsBytes());
        src_size = in.bytes_count;
        break;
    }

    size_t dst_size = base::Base64DecSize(src_size);
    uint8_t* dst = reinterpret_cast<uint8_t*>(malloc(dst_size));
    ssize_t res = base::Base64Decode(src, src_size, dst, dst_size);
    if (res < 0) {
      free(dst);
      return base::ErrStatus("BASE64: Invalid input");
    }
    dst_size = static_cast<size_t>(res);
    out = SqlValue::Bytes(dst, dst_size);
    destructors.bytes_destructor = free;
    return base::OkStatus();
  }
};

}  // namespace

base::Status RegisterBase64Functions(PerfettoSqlEngine& engine) {
  return engine.RegisterStaticFunction<Base64Decode>("base64_decode", 1,
                                                     nullptr, true);
}

}  // namespace perfetto::trace_processor
