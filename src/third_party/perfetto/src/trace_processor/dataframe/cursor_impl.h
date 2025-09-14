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

#ifndef SRC_TRACE_PROCESSOR_DATAFRAME_CURSOR_IMPL_H_
#define SRC_TRACE_PROCESSOR_DATAFRAME_CURSOR_IMPL_H_

#include <cstddef>
#include <cstdint>

#include "src/trace_processor/dataframe/cursor.h"
#include "src/trace_processor/dataframe/impl/bytecode_interpreter_impl.h"  // IWYU pragma: keep
#include "src/trace_processor/dataframe/impl/types.h"

namespace perfetto::trace_processor::dataframe {

template <typename FilterValueFetcherImpl>
void Cursor<FilterValueFetcherImpl>::Execute(
    FilterValueFetcherImpl& filter_value_fetcher) {
  using S = impl::Span<uint32_t>;
  interpreter_.Execute(filter_value_fetcher);

  const auto& span =
      *interpreter_.template GetRegisterValue<S>(params_.output_register);
  pos_ = span.b;
  end_ = span.e;
}

}  // namespace perfetto::trace_processor::dataframe

#endif  // SRC_TRACE_PROCESSOR_DATAFRAME_CURSOR_IMPL_H_
