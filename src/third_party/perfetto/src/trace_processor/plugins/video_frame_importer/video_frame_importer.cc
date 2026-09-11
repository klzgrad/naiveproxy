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

#include "src/trace_processor/plugins/video_frame_importer/video_frame_importer.h"

#include <algorithm>
#include <cstdint>
#include <limits>
#include <memory>
#include <utility>
#include <vector>

#include "perfetto/base/compiler.h"
#include "perfetto/base/logging.h"
#include "perfetto/trace_processor/trace_blob_view.h"
#include "src/trace_processor/core/plugin/plugin.h"
#include "src/trace_processor/core/plugin/registration.h"
#include "src/trace_processor/importers/proto/proto_importer_module.h"
#include "src/trace_processor/perfetto_sql/engine/perfetto_sql_connection.h"
#include "src/trace_processor/plugins/video_frame_importer/tables_py.h"
#include "src/trace_processor/plugins/video_frame_importer/video_frame_module.h"
#include "src/trace_processor/sqlite/bindings/sqlite_function.h"
#include "src/trace_processor/sqlite/bindings/sqlite_result.h"
#include "src/trace_processor/sqlite/bindings/sqlite_type.h"
#include "src/trace_processor/sqlite/bindings/sqlite_value.h"
#include "src/trace_processor/sqlite/sqlite_utils.h"
#include "src/trace_processor/storage/trace_storage.h"
#include "src/trace_processor/types/trace_processor_context.h"

namespace perfetto::trace_processor::video_frame_importer {
namespace {

// Returns the raw encoded bytes (Annex-B au_data, or codec_config) for one
// __intrinsic_video_frames row. The bytes are a zero-copy view into the
// original trace blob, kept alive by the owning vector for the session, so
// SQLite is given a static pointer.
struct VideoFrameAuData : public sqlite::Function<VideoFrameAuData> {
  static constexpr char kName[] = "__intrinsic_video_frame_au_data";
  static constexpr int kArgCount = 1;
  using UserData = std::vector<TraceBlobView>;

  static void Step(sqlite3_context* ctx, int argc, sqlite3_value** argv) {
    PERFETTO_DCHECK(argc == 1);
    if (sqlite::value::Type(argv[0]) == sqlite::Type::kNull) {
      return sqlite::utils::ReturnNullFromFunction(ctx);
    }
    const auto& blobs = *GetUserData(ctx);
    auto id = static_cast<uint32_t>(sqlite::value::Int64(argv[0]));
    if (id >= blobs.size()) {
      return sqlite::utils::ReturnNullFromFunction(ctx);
    }
    const TraceBlobView& v = blobs[id];
    sqlite::result::StaticBytes(ctx, v.data(), static_cast<int>(v.size()));
  }
};

// The plugin owns the __intrinsic_video_frames table and the parallel au_data
// payload vector (indexed by row id): both are populated by VideoFrameModule
// during parsing and live for the whole session.
class VideoFrameImporter : public Plugin<VideoFrameImporter> {
 public:
  ~VideoFrameImporter() override;

  void RegisterDataframes(std::vector<PluginDataframe>& out) override {
    EnsureTable();
    out.push_back(
        {&table_->dataframe(), tables::AndroidVideoFramesTable::Name(), {}});
  }

  void RegisterProtoImporterModules(
      ProtoImporterModuleContext* module_context,
      TraceProcessorContext* trace_context) override {
    EnsureTable();
    module_context->modules.emplace_back(new VideoFrameModule(
        module_context, trace_context, table_.get(), &au_data_));
  }

  void RegisterFunctions(PerfettoSqlConnection*,
                         std::vector<FunctionRegistration>& out) override {
    out.push_back(MakeFunctionRegistration<VideoFrameAuData>(&au_data_));
  }

  uint64_t GetBoundsMutationCount() override {
    return table_ ? table_->mutations() : 0;
  }

  std::pair<int64_t, int64_t> GetTimestampBounds() override {
    int64_t start_ns = std::numeric_limits<int64_t>::max();
    int64_t end_ns = 0;
    if (table_) {
      for (auto it = table_->IterateRows(); it; ++it) {
        start_ns = std::min(it.ts(), start_ns);
        end_ns = std::max(it.ts(), end_ns);
      }
    }
    return {start_ns, end_ns};
  }

 private:
  void EnsureTable() {
    if (!table_) {
      table_ = std::make_unique<tables::AndroidVideoFramesTable>(
          trace_context_->storage->mutable_string_pool());
    }
  }

  std::unique_ptr<tables::AndroidVideoFramesTable> table_;
  std::vector<TraceBlobView> au_data_;
};

VideoFrameImporter::~VideoFrameImporter() = default;

}  // namespace

void RegisterPlugin() {
  static PluginRegistration reg(
      []() -> std::unique_ptr<PluginBase> {
        return std::make_unique<VideoFrameImporter>();
      },
      VideoFrameImporter::kPluginId, VideoFrameImporter::kDepIds.data(),
      VideoFrameImporter::kDepIds.size());
  base::ignore_result(reg);
}

}  // namespace perfetto::trace_processor::video_frame_importer
