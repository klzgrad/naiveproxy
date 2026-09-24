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

#include "src/trace_processor/plugins/trace_export/perfetto_export_reader.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory>
#include <string>
#include <utility>

#include "perfetto/base/status.h"
#include "perfetto/ext/base/status_macros.h"
#include "perfetto/trace_processor/trace_blob_view.h"
#include "src/trace_processor/core/dataframe/arrow_deserializer.h"
#include "src/trace_processor/core/dataframe/dataframe.h"
#include "src/trace_processor/storage/trace_storage.h"
#include "src/trace_processor/tables/metadata_tables_py.h"
#include "src/trace_processor/types/trace_manifest_state.h"
#include "src/trace_processor/types/trace_processor_context.h"
#include "src/trace_processor/util/trace_type.h"

namespace perfetto::trace_processor::trace_export {

PerfettoExportTableReader::PerfettoExportTableReader(
    TraceProcessorContext* context,
    PerfettoExportPluginState* state,
    uint32_t file_id)
    : context_(context), state_(state), file_id_(file_id) {}

PerfettoExportTableReader::~PerfettoExportTableReader() = default;

base::Status PerfettoExportTableReader::Parse(TraceBlobView blob) {
  buffer_.PushBack(std::move(blob));
  return base::OkStatus();
}

base::Status PerfettoExportTableReader::OnPushDataToSorter() {
  TraceManifestState* manifest = context_->trace_manifest_state.get();
  if (std::none_of(manifest->files.begin(), manifest->files.end(),
                   [](const TraceManifestState::FileEntry& entry) {
                     return entry.exported_table_schema.has_value();
                   })) {
    return base::ErrStatus(
        "Arrow files can only be loaded from a Perfetto export whose "
        "perfetto_manifest declares each member's "
        "__exported_table_schema");
  }

  // The archive flow guarantees the manifest is parsed before any member, so
  // the first member to get here validates every declared table against this
  // build up front.
  if (!state_->validated) {
    if (!state_->dataframes) {
      return base::ErrStatus(
          "perfetto_export: dataframe registry not initialized");
    }
    ASSIGN_OR_RETURN(state_->tables, ValidatePerfettoExportTables(
                                         manifest->files, *state_->dataframes));
    state_->seen.assign(state_->tables.size(), false);
    state_->validated = true;
  }

  // Resolve which declared table this member is, by its archive path.
  auto row =
      (*context_->storage
            ->mutable_trace_file_table())[tables::TraceFileTable::Id(file_id_)];
  if (!row.name()) {
    return base::ErrStatus("perfetto_export: cannot resolve member file name");
  }
  std::string path =
      context_->storage->string_pool().Get(*row.name()).ToStdString();

  auto table = std::find_if(state_->tables.begin(), state_->tables.end(),
                            [&](const ResolvedPerfettoExportTable& candidate) {
                              return candidate.entry->path == path;
                            });
  if (table == state_->tables.end()) {
    return base::ErrStatus(
        "perfetto_export: archive member '%s' has no "
        "__exported_table_schema declaration in the perfetto_manifest",
        path.c_str());
  }
  size_t table_idx = static_cast<size_t>(table - state_->tables.begin());
  if (state_->seen[table_idx]) {
    return base::ErrStatus("perfetto_export: duplicate archive member '%s'",
                           path.c_str());
  }
  state_->seen[table_idx] = true;

  ASSIGN_OR_RETURN(auto dataframe,
                   core::dataframe::DeserializeFromArrow(
                       buffer_, context_->storage->mutable_string_pool(),
                       table->dataframe->CreateSpec()));

  const TraceManifestState::PerfettoExportTable& schema =
      *table->entry->exported_table_schema;
  if (dataframe.row_count() != schema.row_count) {
    return base::ErrStatus(
        "perfetto_export: table '%s' has %u rows but the manifest declares "
        "%u",
        schema.name.c_str(), dataframe.row_count(), schema.row_count);
  }
  *table->dataframe = std::move(dataframe);
  buffer_ = util::TraceBlobViewReader();
  return base::OkStatus();
}

namespace {

// An Arrow file ("ARROW1" magic). Only meaningful to Trace Processor when a
// kPerfetto manifest declares it; standalone files and kArrowTar are rejected.
class PerfettoExportTableImporter
    : public TraceImporter<PerfettoExportTableImporter> {
 public:
  explicit PerfettoExportTableImporter(PerfettoExportPluginState* state)
      : TraceImporter(MakeDescriptor()), state_(state) {}
  ~PerfettoExportTableImporter() override;

  bool Sniff(const uint8_t* data, size_t size) const override {
    static constexpr char kMagic[] = {'A', 'R', 'R', 'O', 'W', '1'};
    return size >= sizeof(kMagic) && memcmp(data, kMagic, sizeof(kMagic)) == 0;
  }

  base::StatusOr<std::unique_ptr<ChunkedTraceReader>> CreateReader(
      TraceProcessorContext* context,
      uint32_t file_id) const override {
    return std::unique_ptr<ChunkedTraceReader>(
        std::make_unique<PerfettoExportTableReader>(context, state_, file_id));
  }

 private:
  static TraceTypeDescriptor MakeDescriptor() {
    TraceTypeDescriptor d;
    d.name = "arrow";
    d.sort_policy = TraceSortPolicy::kNone;
    d.clock_policy = TraceClockPolicy::kNone;
    d.sets_default_clock = false;
    d.claims_global_clock = false;
    // A serialized table restores rows directly: it produces no timeline of
    // its own, so it must not fork a per-trace context.
    d.forks_context = false;
    d.detection_priority = 15;
    return d;
  }

  PerfettoExportPluginState* const state_;
};

PerfettoExportTableImporter::~PerfettoExportTableImporter() = default;

}  // namespace

std::unique_ptr<TraceImporterBase> CreatePerfettoExportTableImporter(
    PerfettoExportPluginState* state) {
  return std::make_unique<PerfettoExportTableImporter>(state);
}

}  // namespace perfetto::trace_processor::trace_export
