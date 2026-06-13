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

#ifndef INCLUDE_PERFETTO_EXT_BASE_VARIANT_H_
#define INCLUDE_PERFETTO_EXT_BASE_VARIANT_H_

#include <cstddef>
#include <variant>

#include "perfetto/base/compiler.h"
#include "perfetto/base/logging.h"

namespace perfetto::base {

namespace variant_internal {

constexpr size_t npos = static_cast<size_t>(-1);
constexpr size_t ambiguous = static_cast<size_t>(-2);

template <class T, class... Ts>
constexpr size_t find_index() {
  constexpr bool matches[] = {std::is_same_v<T, Ts>...};
  size_t result = npos;
  for (size_t i = 0; i < sizeof...(Ts); ++i) {
    if (matches[i]) {
      if (result != npos)
        return ambiguous;  // found more than once
      result = i;
    }
  }
  return result;
}

template <class Variant, class T>
struct variant_index_impl;

template <class T, class... Ts>
struct variant_index_impl<std::variant<Ts...>, T> {
  static constexpr size_t value = find_index<T, Ts...>();
};

}  // namespace variant_internal

// Given a std::variant and a type T, returns the index of the T in the variant.
template <typename VariantType, typename T>
constexpr size_t variant_index() {
  constexpr size_t idx =
      variant_internal::variant_index_impl<VariantType, T>::value;
  static_assert(idx != variant_internal::npos, "Type not found in variant");
  static_assert(idx != variant_internal::ambiguous,
                "Type appears more than once in variant");
  return idx;
}

template <typename T, typename VariantType>
constexpr T& unchecked_get(VariantType& variant) {
  PERFETTO_DCHECK(std::holds_alternative<T>(variant));
  auto* v = std::get_if<T>(&variant);
  PERFETTO_ASSUME(v != nullptr);
  return *v;
}

template <typename T, typename VariantType>
constexpr const T& unchecked_get(const VariantType& variant) {
  PERFETTO_DCHECK(std::holds_alternative<T>(variant));
  const auto* v = std::get_if<T>(&variant);
  PERFETTO_ASSUME(v != nullptr);
  return *v;
}

}  // namespace perfetto::base

#endif  // INCLUDE_PERFETTO_EXT_BASE_VARIANT_H_
