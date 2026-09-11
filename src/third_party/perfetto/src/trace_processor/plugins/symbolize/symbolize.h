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

#ifndef SRC_TRACE_PROCESSOR_PLUGINS_SYMBOLIZE_SYMBOLIZE_H_
#define SRC_TRACE_PROCESSOR_PLUGINS_SYMBOLIZE_SYMBOLIZE_H_

#include "perfetto/base/status.h"
#include "src/trace_processor/containers/string_pool.h"
#include "src/trace_processor/perfetto_sql/engine/perfetto_sql_connection.h"

namespace perfetto::trace_processor::symbolize {

// Registers the __intrinsic_symbolize function with |connection|
void RegisterPlugin();

}  // namespace perfetto::trace_processor::symbolize

#endif  // SRC_TRACE_PROCESSOR_PLUGINS_SYMBOLIZE_SYMBOLIZE_H_
