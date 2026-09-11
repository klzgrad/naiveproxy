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

#include "src/trace_processor/plugins/trace_export/trace_export.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <iterator>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "perfetto/base/logging.h"
#include "perfetto/base/status.h"
#include "perfetto/ext/base/status_macros.h"
#include "perfetto/ext/base/version.h"
#include "src/trace_processor/core/dataframe/arrow_serializer.h"
#include "src/trace_processor/core/dataframe/dataframe.h"
#include "src/trace_processor/core/plugin/plugin.h"
#include "src/trace_processor/plugins/trace_export/perfetto_export_manifest.h"
#include "src/trace_processor/plugins/trace_export/perfetto_export_reader.h"
#include "src/trace_processor/trace_reader_registry.h"
#include "src/trace_processor/util/tar_writer.h"

namespace perfetto::trace_processor::trace_export {
namespace {

// These tables describe the current parsing session rather than trace data.
// Restoring their rows would corrupt trackers in the loading instance. This
// filter is specific to the version-coupled kPerfetto format; kArrowTar exports
// every statically registered dataframe for external consumers.
constexpr std::string_view kSkippedPerfettoTables[] = {
    "__intrinsic_trace_file",  "__intrinsic_trace_import_logs",
    "__intrinsic_build_flags", "__intrinsic_modules",
    "__intrinsic_metadata",    "__intrinsic_stats",
};

bool IsSkippedPerfettoTable(const std::string& name) {
  return std::any_of(std::begin(kSkippedPerfettoTables),
                     std::end(kSkippedPerfettoTables),
                     [&](std::string_view skipped) { return name == skipped; });
}

class ExportTarWriterSink : public util::TarWriterSink {
 public:
  explicit ExportTarWriterSink(TraceProcessor::ExportOutput* output)
      : output_(output) {}

  base::Status Write(const void* data, size_t size) override {
    return output_->Write(data, size);
  }

  base::Status WriteFromFd(int, size_t) override {
    return base::ErrStatus("Export output does not support fd writes");
  }

 private:
  TraceProcessor::ExportOutput* output_;
};

class TraceExportPlugin : public Plugin<TraceExportPlugin> {
 public:
  ~TraceExportPlugin() override;

  void RegisterImporters(TraceReaderRegistry& registry) override {
    registry.Register(CreatePerfettoExportTableImporter(&state_));
  }

  void OnDataframesRegistered(
      const std::vector<PluginDataframe>& dataframes) override {
    state_.dataframes = &dataframes;
  }

 private:
  PerfettoExportPluginState state_;
};

TraceExportPlugin::~TraceExportPlugin() = default;

base::Status WriteArrowTable(util::TarWriter* tar,
                             const PluginDataframe& table,
                             const StringPool& pool,
                             core::dataframe::ArrowSerializer* serializer) {
  ASSIGN_OR_RETURN(size_t size, serializer->Prepare(*table.dataframe, pool));
  ASSIGN_OR_RETURN(auto file_writer,
                   tar->StreamFile(table.name + ".arrow", size));
  RETURN_IF_ERROR(serializer->Write(*table.dataframe, pool,
                                    [&](const uint8_t* data, size_t len) {
                                      return file_writer.Write(data, len);
                                    }));
  return file_writer.Finalize();
}

}  // namespace

void RegisterPlugin() {
  static PluginRegistration reg(
      []() -> std::unique_ptr<PluginBase> {
        return std::make_unique<TraceExportPlugin>();
      },
      TraceExportPlugin::kPluginId, TraceExportPlugin::kDepIds.data(),
      TraceExportPlugin::kDepIds.size());
  base::ignore_result(reg);
}

base::Status WriteExport(const std::vector<PluginDataframe>& dataframes,
                         const StringPool& pool,
                         TraceProcessor::ExportFormat format,
                         TraceProcessor::ExportOutput* output) {
  if (!output) {
    return base::ErrStatus("Export output is null");
  }

  if (format == TraceProcessor::ExportFormat::kSqlite) {
    return base::ErrStatus("SQLite export must use random-access output");
  }

  util::TarWriter tar(std::make_unique<ExportTarWriterSink>(output));
  if (format == TraceProcessor::ExportFormat::kArrowTar ||
      format == TraceProcessor::ExportFormat::kPerfetto) {
    bool perfetto = format == TraceProcessor::ExportFormat::kPerfetto;
    std::vector<const PluginDataframe*> tables;
    for (const PluginDataframe& table : dataframes) {
      if (perfetto && (table.dataframe->row_count() == 0 ||
                       IsSkippedPerfettoTable(table.name))) {
        continue;
      }
      tables.push_back(&table);
    }
    if (perfetto) {
      std::string manifest =
          SerializePerfettoExportManifest(tables, base::GetVersionString());
      RETURN_IF_ERROR(tar.AddFile(
          kPerfettoManifestFileName,
          reinterpret_cast<const uint8_t*>(manifest.data()), manifest.size()));
    }
    core::dataframe::ArrowSerializer serializer(
        perfetto ? core::dataframe::ArrowSerializer::IdColumnMode::kOmit
                 : core::dataframe::ArrowSerializer::IdColumnMode::kInclude);
    for (const PluginDataframe* table : tables) {
      RETURN_IF_ERROR(WriteArrowTable(&tar, *table, pool, &serializer));
    }
  }
  return tar.Finalize();
}

}  // namespace perfetto::trace_processor::trace_export
