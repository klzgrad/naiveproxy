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

#include "src/trace_processor/plugins/core_functions/core_functions.h"

#include <memory>
#include <vector>

#include "perfetto/base/compiler.h"
#include "src/trace_processor/core/plugin/plugin.h"
#include "src/trace_processor/perfetto_sql/engine/perfetto_sql_connection.h"
#include "src/trace_processor/plugins/core_functions/window_functions.h"

namespace perfetto::trace_processor::core_functions {

namespace {

class CoreFunctionsPlugin : public Plugin<CoreFunctionsPlugin> {
 public:
  ~CoreFunctionsPlugin() override;
  void RegisterWindowFunctions(
      PerfettoSqlConnection*,
      std::vector<WindowFunctionRegistration>& out) override {
    out.push_back(
        MakeWindowRegistration<LastNonNull>("LAST_NON_NULL", 1, nullptr));
  }
};
CoreFunctionsPlugin::~CoreFunctionsPlugin() = default;

}  // namespace

void RegisterPlugin() {
  static PluginRegistration reg(
      []() -> std::unique_ptr<PluginBase> {
        return std::make_unique<CoreFunctionsPlugin>();
      },
      CoreFunctionsPlugin::kPluginId, CoreFunctionsPlugin::kDepIds.data(),
      CoreFunctionsPlugin::kDepIds.size());
  base::ignore_result(reg);
}

}  // namespace perfetto::trace_processor::core_functions
