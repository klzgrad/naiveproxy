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

#include <cstdint>
#include <initializer_list>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "src/trace_processor/core/dataframe/specs.h"
#include "src/trace_processor/core/plugin/plugin.h"
#include "src/trace_processor/perfetto_sql/intrinsics/table_functions/static_table_function.h"
#include "src/trace_processor/plugins/wattson/cpu_1d_curves.h"
#include "src/trace_processor/plugins/wattson/cpu_2d_curves.h"
#include "src/trace_processor/plugins/wattson/gpu_curves.h"
#include "src/trace_processor/plugins/wattson/l3_curves.h"
#include "src/trace_processor/plugins/wattson/table_function.h"
#include "src/trace_processor/plugins/wattson/tpu_curves.h"
#include "src/trace_processor/storage/trace_storage.h"
#include "src/trace_processor/types/trace_processor_context.h"

namespace perfetto::trace_processor::wattson {
namespace {

using core::dataframe::ColumnSpec;
using core::dataframe::DataframeSpec;

// Convenience constants for the column shapes the codegen emits. All
// wattson curve columns are NonNull and Unsorted with duplicates allowed.
const ColumnSpec kInt = {core::Int64{}, core::NonNull{}, core::Unsorted{},
                         core::HasDuplicates{}};
const ColumnSpec kDouble = {core::Double{}, core::NonNull{}, core::Unsorted{},
                            core::HasDuplicates{}};
const ColumnSpec kStr = {core::String{}, core::NonNull{}, core::Unsorted{},
                         core::HasDuplicates{}};

DataframeSpec MakeSpec(
    std::initializer_list<std::pair<const char*, ColumnSpec>> columns) {
  DataframeSpec spec;
  spec.column_names.reserve(columns.size());
  spec.column_specs.reserve(columns.size());
  for (const auto& [name, cs] : columns) {
    spec.column_names.emplace_back(name);
    spec.column_specs.push_back(cs);
  }
  return spec;
}

}  // namespace

class WattsonPlugin : public Plugin<WattsonPlugin> {
 public:
  ~WattsonPlugin() override;

  void RegisterStaticTableFunctions(
      std::vector<std::unique_ptr<StaticTableFunction>>& fns) override {
    StringPool* pool = trace_context_->storage->mutable_string_pool();
    fns.emplace_back(std::make_unique<WattsonCurvesTableFunction>(
        pool, "__intrinsic_wattson_curves_cpu_1d",
        MakeSpec({{"device", kStr},
                  {"policy", kInt},
                  {"freq_khz", kInt},
                  {"static", kDouble},
                  {"active", kDouble},
                  {"idle0", kDouble},
                  {"idle1", kDouble}}),
        kCpu1DCurvesData.data(), kCpu1DCurvesData.size()));
    fns.emplace_back(std::make_unique<WattsonCurvesTableFunction>(
        pool, "__intrinsic_wattson_curves_cpu_2d",
        MakeSpec({{"device", kStr},
                  {"policy", kInt},
                  {"freq_khz", kInt},
                  {"dep_policy", kInt},
                  {"dep_freq", kInt},
                  {"static", kDouble},
                  {"active", kDouble},
                  {"idle0", kDouble},
                  {"idle1", kDouble}}),
        kCpu2DCurvesData.data(), kCpu2DCurvesData.size()));
    fns.emplace_back(std::make_unique<WattsonCurvesTableFunction>(
        pool, "__intrinsic_wattson_curves_gpu",
        MakeSpec({{"device", kStr},
                  {"freq_khz", kInt},
                  {"active", kDouble},
                  {"idle1", kDouble},
                  {"idle2", kDouble}}),
        kGpuCurvesData.data(), kGpuCurvesData.size()));
    fns.emplace_back(std::make_unique<WattsonCurvesTableFunction>(
        pool, "__intrinsic_wattson_curves_l3",
        MakeSpec({{"device", kStr},
                  {"freq_khz", kInt},
                  {"dep_policy", kInt},
                  {"dep_freq", kInt},
                  {"l3_hit", kDouble},
                  {"l3_miss", kDouble}}),
        kL3CurvesData.data(), kL3CurvesData.size()));
    fns.emplace_back(std::make_unique<WattsonCurvesTableFunction>(
        pool, "__intrinsic_wattson_curves_tpu",
        MakeSpec({{"device", kStr},
                  {"cluster", kInt},
                  {"requests", kInt},
                  {"freq", kInt},
                  {"active", kDouble}}),
        kTpuCurvesData.data(), kTpuCurvesData.size()));
  }
};

WattsonPlugin::~WattsonPlugin() = default;

PERFETTO_TP_REGISTER_PLUGIN(WattsonPlugin);

}  // namespace perfetto::trace_processor::wattson
