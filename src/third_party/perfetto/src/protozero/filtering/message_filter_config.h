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

#ifndef SRC_PROTOZERO_FILTERING_MESSAGE_FILTER_CONFIG_H_
#define SRC_PROTOZERO_FILTERING_MESSAGE_FILTER_CONFIG_H_

#include "perfetto/base/status.h"
#include "protos/perfetto/config/trace_config.gen.h"
#include "src/protozero/filtering/message_filter.h"

namespace protozero {

// Loads a MessageFilter from the trace_filter field of a TraceConfig.
// This sets up the string filter rules (both base and v54 chains) and loads
// the bytecode (v1/v2 + v54 overlay).
// On failure, returns a non-ok status with a descriptive error message.
// On success, the caller may optionally call SetFilterRoot() to adjust the
// root message (e.g. for per-packet filtering in the tracing service).
perfetto::base::Status LoadMessageFilterConfig(
    const perfetto::protos::gen::TraceConfig::TraceFilter& filt,
    MessageFilter* filter);

}  // namespace protozero

#endif  // SRC_PROTOZERO_FILTERING_MESSAGE_FILTER_CONFIG_H_
