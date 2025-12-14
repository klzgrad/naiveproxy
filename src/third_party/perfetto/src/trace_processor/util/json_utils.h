/*
 * Copyright (C) 2020 The Android Open Source Project
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

#ifndef SRC_TRACE_PROCESSOR_UTIL_JSON_UTILS_H_
#define SRC_TRACE_PROCESSOR_UTIL_JSON_UTILS_H_

#include <optional>
#include <string_view>

#include "perfetto/base/build_config.h"
#include "perfetto/ext/base/string_view.h"
#include "src/trace_processor/importers/common/args_tracker.h"
#include "src/trace_processor/util/json_parser.h"
#include "src/trace_processor/storage/trace_storage.h"

#if PERFETTO_BUILDFLAG(PERFETTO_TP_JSON)
#include <json/value.h>
#else
namespace Json {
class Value {};
}  // namespace Json
#endif

namespace perfetto::trace_processor::json {

// Returns whether JSON related functioanlity is supported with the current
// build flags.
constexpr bool IsJsonSupported() {
#if PERFETTO_BUILDFLAG(PERFETTO_TP_JSON)
  return true;
#else
  return false;
#endif
}

// Parses the given JSON string into a JSON::Value object.
// This function should only be called if |IsJsonSupported()| returns true.
std::optional<Json::Value> ParseJsonString(base::StringView raw_string);

// Flattens the given serialized json value (with bounds `start` and `end`)
// using `it` and adds each leaf node to the bound args inserter.
//
// Note:
//  * |flat_key| and |key| should be non-empty and will be used to prefix the
//    keys of all leaf nodes in the JSON.
//  * |storage| is used to intern all strings (e.g. keys and values).
bool AddJsonValueToArgs(Iterator& it,
                        const char* start,
                        const char* end,
                        std::string_view flat_key,
                        std::string_view key,
                        TraceStorage* storage,
                        ArgsTracker::BoundInserter* inserter);

}  // namespace perfetto::trace_processor::json

#endif  // SRC_TRACE_PROCESSOR_UTIL_JSON_UTILS_H_
