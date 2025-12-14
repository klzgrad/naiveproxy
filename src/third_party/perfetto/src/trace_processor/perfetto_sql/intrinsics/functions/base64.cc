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
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>

#include "perfetto/base/logging.h"
#include "perfetto/base/status.h"
#include "perfetto/ext/base/base64.h"
#include "perfetto/ext/base/utils.h"
#include "src/trace_processor/perfetto_sql/engine/perfetto_sql_engine.h"
#include "src/trace_processor/sqlite/bindings/sqlite_function.h"
#include "src/trace_processor/sqlite/bindings/sqlite_result.h"
#include "src/trace_processor/sqlite/bindings/sqlite_type.h"
#include "src/trace_processor/sqlite/bindings/sqlite_value.h"
#include "src/trace_processor/sqlite/sqlite_utils.h"

namespace perfetto::trace_processor {

namespace {

struct Base64Decode : public sqlite::Function<Base64Decode> {
  static constexpr char kName[] = "base64_decode";
  static constexpr int kArgCount = 1;

  static void Step(sqlite3_context* ctx, int argc, sqlite3_value** argv) {
    PERFETTO_DCHECK(argc == 1);

    const char* src = nullptr;
    size_t src_size = 0;
    switch (sqlite::value::Type(argv[0])) {
      case sqlite::Type::kNull:
        return sqlite::utils::ReturnNullFromFunction(ctx);
      case sqlite::Type::kInteger:
      case sqlite::Type::kFloat:
        return sqlite::utils::SetError(
            ctx, "BASE64: argument must be string or blob");
      case sqlite::Type::kText:
        src = sqlite::value::Text(argv[0]);
        src_size = static_cast<size_t>(sqlite::value::Bytes(argv[0]));
        break;
      case sqlite::Type::kBlob:
        src = reinterpret_cast<const char*>(sqlite::value::Blob(argv[0]));
        src_size = static_cast<size_t>(sqlite::value::Bytes(argv[0]));
        break;
    }

    size_t dst_size = base::Base64DecSize(src_size);
    std::unique_ptr<uint8_t, base::FreeDeleter> dst(
        reinterpret_cast<uint8_t*>(malloc(dst_size)));
    auto res = base::Base64Decode(src, src_size, dst.get(), dst_size);
    if (res < 0) {
      return sqlite::utils::SetError(ctx, "BASE64: Invalid input");
    }
    dst_size = static_cast<size_t>(res);
    return sqlite::result::RawBytes(ctx, dst.release(),
                                    static_cast<int>(dst_size), free);
  }
};

}  // namespace

base::Status RegisterBase64Functions(PerfettoSqlEngine& engine) {
  return engine.RegisterFunction<Base64Decode>(nullptr);
}

}  // namespace perfetto::trace_processor
