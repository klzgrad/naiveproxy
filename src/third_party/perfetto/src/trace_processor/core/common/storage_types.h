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

#ifndef SRC_TRACE_PROCESSOR_CORE_COMMON_STORAGE_TYPES_H_
#define SRC_TRACE_PROCESSOR_CORE_COMMON_STORAGE_TYPES_H_

#include <cstdint>

#include "src/trace_processor/containers/string_pool.h"
#include "src/trace_processor/core/util/type_set.h"

namespace perfetto::trace_processor::core {

// Represents values where the index of the value in the table is the same as
// the value. This allows for zero memory overhead as values don't need to be
// explicitly stored. Operations on column with this type can be highly
// optimized.
struct Id {
  using cpp_type = void;
};

// Represents values where the value is a 32-bit unsigned integer.
struct Uint32 {
  using cpp_type = uint32_t;
};

// Represents values where the value is a 32-bit signed integer.
struct Int32 {
  using cpp_type = int32_t;
};

// Represents values where the value is a 64-bit signed integer.
struct Int64 {
  using cpp_type = int64_t;
};

// Represents values where the value is a double.
struct Double {
  using cpp_type = double;
};

// Represents values where the value is a string.
struct String {
  using cpp_type = StringPool::Id;
};

// TypeSet of all possible storage value types.
using StorageType = core::TypeSet<Id, Uint32, Int32, Int64, Double, String>;

// Maps a C++ type to its corresponding storage type tag.
// E.g., TypeTagFor<int64_t>::type = Int64
template <typename CppType>
struct TypeTagFor;

template <>
struct TypeTagFor<uint32_t> {
  using type = Uint32;
};
template <>
struct TypeTagFor<int32_t> {
  using type = Int32;
};
template <>
struct TypeTagFor<int64_t> {
  using type = Int64;
};
template <>
struct TypeTagFor<double> {
  using type = Double;
};
template <>
struct TypeTagFor<StringPool::Id> {
  using type = String;
};

}  // namespace perfetto::trace_processor::core

#endif  // SRC_TRACE_PROCESSOR_CORE_COMMON_STORAGE_TYPES_H_
