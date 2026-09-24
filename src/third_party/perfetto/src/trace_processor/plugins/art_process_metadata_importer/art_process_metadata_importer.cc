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

#include "src/trace_processor/plugins/art_process_metadata_importer/art_process_metadata_importer.h"

#include <memory>

#include "perfetto/base/compiler.h"
#include "src/trace_processor/core/plugin/plugin.h"
#include "src/trace_processor/importers/proto/proto_importer_module.h"
#include "src/trace_processor/plugins/art_process_metadata_importer/art_process_metadata_module.h"

namespace perfetto::trace_processor::art_process_metadata_importer {

class ArtProcessMetadataImporter : public Plugin<ArtProcessMetadataImporter> {
 public:
  ~ArtProcessMetadataImporter() override;

  void RegisterProtoImporterModules(
      ProtoImporterModuleContext* module_context,
      TraceProcessorContext* trace_context) override {
    module_context->modules.emplace_back(
        new ArtProcessMetadataModule(module_context, trace_context));
  }
};

ArtProcessMetadataImporter::~ArtProcessMetadataImporter() = default;

void RegisterPlugin() {
  static PluginRegistration reg(
      []() -> std::unique_ptr<PluginBase> {
        return std::make_unique<ArtProcessMetadataImporter>();
      },
      ArtProcessMetadataImporter::kPluginId,
      ArtProcessMetadataImporter::kDepIds.data(),
      ArtProcessMetadataImporter::kDepIds.size());
  base::ignore_result(reg);
}

}  // namespace perfetto::trace_processor::art_process_metadata_importer
