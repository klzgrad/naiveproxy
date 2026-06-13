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

#ifndef INCLUDE_PERFETTO_EXT_BASE_HASH_H_
#define INCLUDE_PERFETTO_EXT_BASE_HASH_H_

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <tuple>
#include <utility>

namespace perfetto::base {

// ============================================================================
// Absl-style hash customization point
// ============================================================================
//
// To make a type hashable with Perfetto hash functions, define a friend
// function template:
//
//   template <typename H>
//   friend H PerfettoHashValue(H h, const MyType& value) {
//     return H::Combine(std::move(h), value.field1, value.field2);
//   }
//
// This function will be found via ADL (Argument Dependent Lookup) when hashing
// your type. No forward declaration is needed - ADL finds it in your type's
// namespace.

// ============================================================================
// Built-in PerfettoHashValue implementations for common standard library
// types. These allow standard library types to work seamlessly with the
// absl-style hash API without requiring users to define their own
// implementations.
// ============================================================================

// Hash function for std::optional - hashes the value if present, or a sentinel
// if not.
template <typename H, typename T>
H PerfettoHashValue(H h, const std::optional<T>& value) {
  if (value.has_value()) {
    return H::Combine(H::Combine(std::move(h), true), *value);
  }
  return H::Combine(std::move(h), false, 0);
}

// Hash function for std::pair - combines hashes of both elements.
template <typename H, typename T1, typename T2>
H PerfettoHashValue(H h, const std::pair<T1, T2>& value) {
  return H::Combine(std::move(h), value.first, value.second);
}

// Hash function for std::tuple - combines hashes of all elements.
template <typename H, typename... Ts>
H PerfettoHashValue(H h, const std::tuple<Ts...>& value) {
  return std::apply(
      [&h](const auto&... elements) {
        return H::Combine(std::move(h), elements...);
      },
      value);
}

// Hash function for pointers - hashes the pointer value as an integer.
template <typename H, typename T>
H PerfettoHashValue(H h, const std::unique_ptr<T>& ptr) {
  return H::Combine(std::move(h), ptr.get());
}
template <typename H, typename T>
H PerfettoHashValue(H h, const std::shared_ptr<T>& ptr) {
  return H::Combine(std::move(h), ptr.get());
}

// This is for using already-hashed key into std::unordered_map and avoid the
// cost of re-hashing. Example:
// unordered_map<uint64_t, Value, AlreadyHashed> my_map.
template <typename T>
struct AlreadyHashed {
  size_t operator()(const T& x) const { return static_cast<size_t>(x); }
};

}  // namespace perfetto::base

#endif  // INCLUDE_PERFETTO_EXT_BASE_HASH_H_
