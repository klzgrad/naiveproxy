/*
 * Copyright (C) 2019 The Android Open Source Project
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

#ifndef SRC_TRACE_PROCESSOR_TYPES_GFP_FLAGS_H_
#define SRC_TRACE_PROCESSOR_TYPES_GFP_FLAGS_H_

#include <optional>

#include "perfetto/ext/base/string_writer.h"
#include "src/trace_processor/types/version_number.h"

namespace perfetto {
namespace trace_processor {

// GFP flags in ftrace events should be parsed and read differently depending
// the kernel version. This function writes a human readable version of the
// flag.
void WriteGfpFlag(uint64_t value,
                  std::optional<VersionNumber> version,
                  base::StringWriter* writer);

}  // namespace trace_processor
}  // namespace perfetto

#endif  // SRC_TRACE_PROCESSOR_TYPES_GFP_FLAGS_H_
