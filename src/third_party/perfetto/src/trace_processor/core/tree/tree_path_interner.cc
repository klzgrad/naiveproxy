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

#include "src/trace_processor/core/tree/tree_path_interner.h"

#include <cstddef>
#include <cstdint>

#include "perfetto/ext/base/utils.h"

namespace perfetto::trace_processor::core {

TreePathInterner::TreePathInterner(uint32_t estimated_nodes)
    : nodes_(base::RoundUpToPowerOfTwo(static_cast<size_t>(estimated_nodes))),
      parent_(FlexVector<uint32_t>::CreateWithCapacity(estimated_nodes)),
      representative_row_(
          FlexVector<uint32_t>::CreateWithCapacity(estimated_nodes)) {}

}  // namespace perfetto::trace_processor::core
