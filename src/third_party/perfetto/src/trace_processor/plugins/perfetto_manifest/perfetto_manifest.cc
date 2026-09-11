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

#include "src/trace_processor/plugins/perfetto_manifest/perfetto_manifest.h"

#include <memory>

#include "perfetto/base/compiler.h"
#include "src/trace_processor/core/plugin/plugin.h"
#include "src/trace_processor/plugins/perfetto_manifest/perfetto_manifest_reader.h"
#include "src/trace_processor/trace_reader_registry.h"
#include "src/trace_processor/util/trace_type.h"

namespace perfetto::trace_processor::perfetto_manifest {
namespace {

class PerfettoManifestPlugin : public Plugin<PerfettoManifestPlugin> {
 public:
  ~PerfettoManifestPlugin() override;

  void RegisterImporters(TraceReaderRegistry& registry) override {
    registry.Register(CreatePerfettoManifestImporter());
  }
};

PerfettoManifestPlugin::~PerfettoManifestPlugin() = default;

}  // namespace

void RegisterPlugin() {
  static PluginRegistration reg(
      []() -> std::unique_ptr<PluginBase> {
        return std::make_unique<PerfettoManifestPlugin>();
      },
      PerfettoManifestPlugin::kPluginId, PerfettoManifestPlugin::kDepIds.data(),
      PerfettoManifestPlugin::kDepIds.size());
  base::ignore_result(reg);
}

}  // namespace perfetto::trace_processor::perfetto_manifest
