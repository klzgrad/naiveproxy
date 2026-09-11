/*
 * Copyright (C) 2024 The Android Open Source Project
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

#ifndef SRC_TRACE_PROCESSOR_PLUGINS_DFS_WEIGHT_BOUNDED_DFS_WEIGHT_BOUNDED_H_
#define SRC_TRACE_PROCESSOR_PLUGINS_DFS_WEIGHT_BOUNDED_DFS_WEIGHT_BOUNDED_H_

namespace perfetto::trace_processor::dfs_weight_bounded {

// Registers the DfsWeightBounded plugin with the global plugin set. Idempotent;
// only the first call has an effect. Must run before the first GetPluginSet()
// call (i.e. before constructing TraceProcessorImpl).
void RegisterPlugin();

}  // namespace perfetto::trace_processor::dfs_weight_bounded

#endif  // SRC_TRACE_PROCESSOR_PLUGINS_DFS_WEIGHT_BOUNDED_DFS_WEIGHT_BOUNDED_H_
