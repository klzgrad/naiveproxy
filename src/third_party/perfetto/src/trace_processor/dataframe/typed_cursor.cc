/*
 * Copyright (C) 2025 The Android Open Source Project
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

#include "src/trace_processor/dataframe/typed_cursor.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <limits>

#include "perfetto/base/logging.h"
#include "perfetto/public/compiler.h"
#include "src/trace_processor/dataframe/cursor_impl.h"  // IWYU pragma: keep

namespace perfetto::trace_processor::dataframe {

void TypedCursor::ExecuteUnchecked() {
  if (PERFETTO_UNLIKELY(last_execution_mutation_count_ !=
                        dataframe_->mutations_)) {
    PrepareCursorInternal();
  }
  Fetcher fetcher{{}, filter_values_.data()};
  cursor_.Execute(fetcher);
}

void TypedCursor::PrepareCursorInternal() {
  auto plan = dataframe_->PlanQuery(filter_specs_, {}, sort_specs_, {}, 0);
  PERFETTO_CHECK(plan.ok());
  dataframe_->PrepareCursor(*plan, cursor_);
  last_execution_mutation_count_ = dataframe_->mutations_;
  for (const auto& spec : filter_specs_) {
    filter_value_mapping_[spec.source_index] =
        spec.value_index.value_or(std::numeric_limits<uint32_t>::max());
  }
  std::fill(filter_values_.begin(), filter_values_.end(), nullptr);
}

}  // namespace perfetto::trace_processor::dataframe
