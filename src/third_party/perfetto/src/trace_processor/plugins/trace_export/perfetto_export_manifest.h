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

#ifndef SRC_TRACE_PROCESSOR_PLUGINS_TRACE_EXPORT_PERFETTO_EXPORT_MANIFEST_H_
#define SRC_TRACE_PROCESSOR_PLUGINS_TRACE_EXPORT_PERFETTO_EXPORT_MANIFEST_H_

#include <cstdint>
#include <string>
#include <vector>

#include "perfetto/ext/base/status_or.h"
#include "src/trace_processor/core/plugin/registration.h"
#include "src/trace_processor/types/trace_manifest_state.h"

namespace perfetto::trace_processor::trace_export {

// kPerfetto archives use a perfetto_manifest to associate each Arrow member
// with the static dataframe into which it must be restored. This manifest is
// internal to Perfetto. Loading it in a different version may work but is not
// guaranteed.
constexpr char kPerfettoManifestFileName[] = "perfetto_manifest.json";
constexpr int64_t kPerfettoExportFormatVersion = 1;

std::string SerializePerfettoExportManifest(
    const std::vector<const PluginDataframe*>& tables,
    const char* writer_version);

struct ResolvedPerfettoExportTable {
  const TraceManifestState::FileEntry* entry = nullptr;
  core::dataframe::Dataframe* dataframe = nullptr;
};

// Validates all table entries against the static dataframes registered in this
// build and returns them in declaration order. The target dataframes must be
// empty and have schemas compatible with the exporter.
base::StatusOr<std::vector<ResolvedPerfettoExportTable>>
ValidatePerfettoExportTables(
    const std::vector<TraceManifestState::FileEntry>& files,
    const std::vector<PluginDataframe>& live);

}  // namespace perfetto::trace_processor::trace_export

#endif  // SRC_TRACE_PROCESSOR_PLUGINS_TRACE_EXPORT_PERFETTO_EXPORT_MANIFEST_H_
