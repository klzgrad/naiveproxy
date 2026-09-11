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

#include "src/trace_processor/plugins/time_functions/time_functions.h"

#include <memory>
#include <vector>

#include "perfetto/base/compiler.h"
#include "src/trace_processor/core/plugin/plugin.h"
#include "src/trace_processor/perfetto_sql/engine/perfetto_sql_connection.h"
#include "src/trace_processor/plugins/time_functions/clock_functions.h"
#include "src/trace_processor/plugins/time_functions/value_at_max_ts.h"
#include "src/trace_processor/types/trace_processor_context.h"

namespace perfetto::trace_processor::time_functions {

namespace {

class TimeFunctionsPlugin : public Plugin<TimeFunctionsPlugin> {
 public:
  ~TimeFunctionsPlugin() override;
  void RegisterFunctions(PerfettoSqlConnection*,
                         std::vector<FunctionRegistration>& out) override {
    auto* cc = trace_context_->clock_converter.get();
    out.push_back(MakeFunctionRegistration<AbsTimeStr>(cc));
    out.push_back(MakeFunctionRegistration<ToMonotonic>(cc));
    out.push_back(MakeFunctionRegistration<ToRealtime>(cc));
    out.push_back(MakeFunctionRegistration<ToTimecode>(nullptr));
  }
  void RegisterAggregateFunctions(
      PerfettoSqlConnection*,
      std::vector<AggregateFunctionRegistration>& out) override {
    out.push_back(MakeAggregateRegistration<ValueAtMaxTs>(nullptr));
  }
};
TimeFunctionsPlugin::~TimeFunctionsPlugin() = default;

}  // namespace

void RegisterPlugin() {
  static PluginRegistration reg(
      []() -> std::unique_ptr<PluginBase> {
        return std::make_unique<TimeFunctionsPlugin>();
      },
      TimeFunctionsPlugin::kPluginId, TimeFunctionsPlugin::kDepIds.data(),
      TimeFunctionsPlugin::kDepIds.size());
  base::ignore_result(reg);
}

}  // namespace perfetto::trace_processor::time_functions
