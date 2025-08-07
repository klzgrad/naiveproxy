/*
 * Copyright (C) 2022 The Android Open Source Project
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

#ifndef INCLUDE_PERFETTO_EXT_TRACE_PROCESSOR_DEMANGLE_H_
#define INCLUDE_PERFETTO_EXT_TRACE_PROCESSOR_DEMANGLE_H_

#include "perfetto/ext/base/utils.h"

namespace perfetto {
namespace trace_processor {
namespace demangle {

// Returns a |malloc|-allocated C string with the demangled name.
// Returns an empty pointer if demangling was unsuccessful.
std::unique_ptr<char, base::FreeDeleter> Demangle(const char* mangled_name);

}  // namespace demangle
}  // namespace trace_processor
}  // namespace perfetto

#endif  // INCLUDE_PERFETTO_EXT_TRACE_PROCESSOR_DEMANGLE_H_
