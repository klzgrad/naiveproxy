// Copyright (C) 2026 The Android Open Source Project
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

#include "src/trace_processor/plugins/zstd_functions/zstd_functions.h"

#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <memory>
#include <vector>

#include "perfetto/base/logging.h"
#include "perfetto/ext/base/utils.h"
#include "src/trace_processor/core/plugin/plugin.h"
#include "src/trace_processor/perfetto_sql/engine/perfetto_sql_connection.h"
#include "src/trace_processor/sqlite/bindings/sqlite_function.h"
#include "src/trace_processor/sqlite/bindings/sqlite_result.h"
#include "src/trace_processor/sqlite/bindings/sqlite_type.h"
#include "src/trace_processor/sqlite/bindings/sqlite_value.h"
#include "src/trace_processor/sqlite/sqlite_utils.h"
#include "src/trace_processor/util/decompressor.h"
#include "src/trace_processor/util/zstd_compressor.h"

namespace perfetto::trace_processor {

namespace {

// A good ratio/speed balance for the compressible text (Chrome JSON etc.) this
// is meant to store.
constexpr int kLevel = 9;

// __intrinsic_zstd_compress(X) -> zstd(X) as a BLOB. X is a string or blob;
// NULL yields NULL.
struct ZstdCompress : public sqlite::Function<ZstdCompress> {
  static constexpr char kName[] = "__intrinsic_zstd_compress";
  static constexpr int kArgCount = 1;

  static void Step(sqlite3_context* ctx, int, sqlite3_value** argv) {
    const uint8_t* src = nullptr;
    size_t src_size = 0;
    switch (sqlite::value::Type(argv[0])) {
      case sqlite::Type::kNull:
        return sqlite::utils::ReturnNullFromFunction(ctx);
      case sqlite::Type::kInteger:
      case sqlite::Type::kFloat:
        return sqlite::utils::SetError(
            ctx, "ZSTD: argument must be a string or blob");
      case sqlite::Type::kText:
        src = reinterpret_cast<const uint8_t*>(sqlite::value::Text(argv[0]));
        src_size = static_cast<size_t>(sqlite::value::Bytes(argv[0]));
        break;
      case sqlite::Type::kBlob:
        src = reinterpret_cast<const uint8_t*>(sqlite::value::Blob(argv[0]));
        src_size = static_cast<size_t>(sqlite::value::Bytes(argv[0]));
        break;
    }

    if (!util::IsZstdSupported()) {
      return sqlite::utils::SetError(
          ctx, "ZSTD: zstd is not compiled into this build");
    }
    size_t out_size = 0;
    auto out =
        util::ZstdCompressor::CompressFully(src, src_size, &out_size, kLevel);
    if (!out) {
      return sqlite::utils::SetError(ctx, "ZSTD: compression failed");
    }
    return sqlite::result::RawBytes(ctx, out.release(),
                                    static_cast<int>(out_size), free);
  }
};

}  // namespace

}  // namespace perfetto::trace_processor

namespace perfetto::trace_processor::zstd_functions {
namespace {

class ZstdFunctionsPlugin : public Plugin<ZstdFunctionsPlugin> {
 public:
  ~ZstdFunctionsPlugin() override;
  void RegisterFunctions(PerfettoSqlConnection*,
                         std::vector<FunctionRegistration>& out) override {
    out.push_back(MakeFunctionRegistration<ZstdCompress>(nullptr));
  }
};
ZstdFunctionsPlugin::~ZstdFunctionsPlugin() = default;

}  // namespace

void RegisterPlugin() {
  static PluginRegistration reg(
      []() -> std::unique_ptr<PluginBase> {
        return std::make_unique<ZstdFunctionsPlugin>();
      },
      ZstdFunctionsPlugin::kPluginId, ZstdFunctionsPlugin::kDepIds.data(),
      ZstdFunctionsPlugin::kDepIds.size());
  base::ignore_result(reg);
}

}  // namespace perfetto::trace_processor::zstd_functions
