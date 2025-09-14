/*
 * Copyright (C) 2023 The Android Open Source Project
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

#include "src/trace_processor/perfetto_sql/intrinsics/table_functions/dataframe_query_plan_decoder.h"

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "perfetto/base/logging.h"
#include "perfetto/base/status.h"
#include "perfetto/ext/base/string_view.h"
#include "perfetto/trace_processor/basic_types.h"
#include "src/trace_processor/containers/string_pool.h"
#include "src/trace_processor/dataframe/dataframe.h"
#include "src/trace_processor/dataframe/specs.h"
#include "src/trace_processor/perfetto_sql/intrinsics/table_functions/static_table_function.h"
#include "src/trace_processor/perfetto_sql/intrinsics/table_functions/tables_py.h"

namespace perfetto::trace_processor {

DataframeQueryPlanDecoder::Cursor::Cursor(StringPool* pool)
    : string_pool_(pool), table_(pool) {}

bool DataframeQueryPlanDecoder::Cursor::Run(
    const std::vector<SqlValue>& arguments) {
  PERFETTO_DCHECK(arguments.size() == 1);
  if (arguments[0].type != SqlValue::kString) {
    return OnFailure(base::ErrStatus(
        "__intrinsic_dataframe_query_plan_decoder takes the serialized query "
        "plan as a string."));
  }

  table_.Clear();
  if (arguments[0].is_null()) {
    // Nothing matches a null plan so return an empty table.
    return OnSuccess(&table_.dataframe());
  }

  const std::string& serialized_query_plan = arguments[0].AsString();
  auto plan =
      dataframe::Dataframe::QueryPlan::Deserialize(serialized_query_plan);
  for (const auto& bc : plan.BytecodeToString()) {
    table_.Insert(tables::DataframeQueryPlanDecoderTable::Row(
        string_pool_->InternString(base::StringView(bc))));
  }
  return OnSuccess(&table_.dataframe());
}

DataframeQueryPlanDecoder::DataframeQueryPlanDecoder(StringPool* pool)
    : string_pool_(pool) {}

std::unique_ptr<StaticTableFunction::Cursor>
DataframeQueryPlanDecoder::MakeCursor() {
  return std::make_unique<Cursor>(string_pool_);
}

dataframe::DataframeSpec DataframeQueryPlanDecoder::CreateSpec() {
  return tables::DataframeQueryPlanDecoderTable::kSpec.ToUntypedDataframeSpec();
}

std::string DataframeQueryPlanDecoder::TableName() {
  return "__intrinsic_dataframe_query_plan_decoder";
}

uint32_t DataframeQueryPlanDecoder::GetArgumentCount() const {
  return 1;
}

}  // namespace perfetto::trace_processor
