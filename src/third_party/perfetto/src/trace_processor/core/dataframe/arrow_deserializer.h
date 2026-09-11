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

#ifndef SRC_TRACE_PROCESSOR_CORE_DATAFRAME_ARROW_DESERIALIZER_H_
#define SRC_TRACE_PROCESSOR_CORE_DATAFRAME_ARROW_DESERIALIZER_H_

#include "perfetto/ext/base/status_or.h"
#include "src/trace_processor/core/dataframe/dataframe.h"

namespace perfetto::trace_processor {
class StringPool;
namespace util {
class TraceBlobViewReader;
}
namespace core::dataframe {

// Reads the single-record-batch Arrow layout emitted by ArrowSerializer and
// returns a newly constructed, finalized dataframe. |spec| supplies the full
// dataframe schema, including properties which are not represented by Arrow,
// such as null storage, sort, and duplicate state.
//
// This deliberately validates only the supported layout; use a full Arrow
// implementation when diagnostics for arbitrary Arrow files are required.
base::StatusOr<Dataframe> DeserializeFromArrow(const util::TraceBlobViewReader&,
                                               StringPool*,
                                               const DataframeSpec&);

}  // namespace core::dataframe
}  // namespace perfetto::trace_processor

#endif  // SRC_TRACE_PROCESSOR_CORE_DATAFRAME_ARROW_DESERIALIZER_H_
