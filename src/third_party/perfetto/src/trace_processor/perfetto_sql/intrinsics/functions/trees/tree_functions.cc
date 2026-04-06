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

#include "src/trace_processor/perfetto_sql/intrinsics/functions/trees/tree_functions.h"

#include "perfetto/base/status.h"
#include "perfetto/ext/base/status_macros.h"
#include "src/trace_processor/containers/string_pool.h"
#include "src/trace_processor/perfetto_sql/engine/perfetto_sql_engine.h"
#include "src/trace_processor/perfetto_sql/intrinsics/functions/trees/tree_conversion.h"
#include "src/trace_processor/perfetto_sql/intrinsics/functions/trees/tree_filter.h"

namespace perfetto::trace_processor {

base::Status RegisterTreeFunctions(PerfettoSqlEngine& engine,
                                   StringPool& pool) {
  RETURN_IF_ERROR(engine.RegisterAggregateFunction<TreeFromTable>(&pool));
  RETURN_IF_ERROR(engine.RegisterFunction<TreeToTable>(nullptr));
  RETURN_IF_ERROR(engine.RegisterFunction<TreeConstraint>(nullptr));
  RETURN_IF_ERROR(engine.RegisterFunction<TreeWhereAnd>(nullptr));
  RETURN_IF_ERROR(engine.RegisterFunction<TreeFilter>(nullptr));
  return base::OkStatus();
}

}  // namespace perfetto::trace_processor
