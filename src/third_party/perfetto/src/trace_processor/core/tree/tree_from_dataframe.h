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

#ifndef SRC_TRACE_PROCESSOR_CORE_TREE_TREE_FROM_DATAFRAME_H_
#define SRC_TRACE_PROCESSOR_CORE_TREE_TREE_FROM_DATAFRAME_H_

#include "perfetto/ext/base/status_or.h"
#include "src/trace_processor/core/dataframe/adhoc_dataframe_builder.h"
#include "src/trace_processor/core/dataframe/runtime_dataframe_builder.h"
#include "src/trace_processor/core/tree/tree.h"

namespace perfetto::trace_processor::core {

// Converts columns collected in an AdhocDataframeBuilder into Tree.
// The first two columns must be integer id and parent_id columns. This
// validates their relationships and normalizes parent ids to row indices.
base::StatusOr<Tree> BuildTree(dataframe::AdhocDataframeBuilder&& builder);
base::StatusOr<Tree> BuildTree(dataframe::RuntimeDataframeBuilder&& builder);

}  // namespace perfetto::trace_processor::core

#endif  // SRC_TRACE_PROCESSOR_CORE_TREE_TREE_FROM_DATAFRAME_H_
