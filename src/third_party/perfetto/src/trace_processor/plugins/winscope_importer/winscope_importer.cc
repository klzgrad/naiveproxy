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

#include "src/trace_processor/plugins/winscope_importer/winscope_importer.h"

#include <memory>

#include "perfetto/base/compiler.h"
#include "src/trace_processor/core/plugin/plugin.h"
#include "src/trace_processor/importers/proto/proto_importer_module.h"
#include "src/trace_processor/plugins/winscope_importer/winscope_module.h"

namespace perfetto::trace_processor::winscope_importer {

class WinscopeImporter : public Plugin<WinscopeImporter> {
 public:
  ~WinscopeImporter() override;

  void RegisterProtoImporterModules(
      ProtoImporterModuleContext* module_context,
      TraceProcessorContext* trace_context) override {
    module_context->modules.emplace_back(
        new WinscopeModule(module_context, trace_context));
  }
};

WinscopeImporter::~WinscopeImporter() = default;

void RegisterPlugin() {
  static PluginRegistration reg(
      []() -> std::unique_ptr<PluginBase> {
        return std::make_unique<WinscopeImporter>();
      },
      WinscopeImporter::kPluginId, WinscopeImporter::kDepIds.data(),
      WinscopeImporter::kDepIds.size());
  base::ignore_result(reg);
}

}  // namespace perfetto::trace_processor::winscope_importer
