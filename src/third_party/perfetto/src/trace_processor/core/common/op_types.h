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

#ifndef SRC_TRACE_PROCESSOR_CORE_COMMON_OP_TYPES_H_
#define SRC_TRACE_PROCESSOR_CORE_COMMON_OP_TYPES_H_

#include "src/trace_processor/core/util/type_set.h"

namespace perfetto::trace_processor::core {

// Filters only cells which compare equal to the given value.
struct Eq {};

// Filters only cells which do not compare equal to the given value.
struct Ne {};

// Filters only cells which are less than the given value.
struct Lt {};

// Filters only cells which are less than or equal to the given value.
struct Le {};

// Filters only cells which are greater than the given value.
struct Gt {};

// Filters only cells which are greater than or equal to the given value.
struct Ge {};

// Filters only cells which match the given glob pattern.
struct Glob {};

// Filters only cells which match the given regex pattern.
struct Regex {};

// Filters only cells which are not NULL.
struct IsNotNull {};

// Filters only cells which are NULL.
struct IsNull {};

// Filters only cells which are part of the provided list of values.
struct In {};

// TypeSet of all possible operations for filter conditions.
using Op =
    core::TypeSet<Eq, Ne, Lt, Le, Gt, Ge, Glob, Regex, IsNotNull, IsNull, In>;

}  // namespace perfetto::trace_processor::core

#endif  // SRC_TRACE_PROCESSOR_CORE_COMMON_OP_TYPES_H_
