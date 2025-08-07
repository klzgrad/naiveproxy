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

// Given a std::variant and a type T, returns the index of the T in the variant.
template <typename VariantType, typename T, size_t i = 0>
constexpr size_t variant_index() {
  static_assert(i < std::variant_size_v<VariantType>,
                "Type not found in variant");
  if constexpr (std::is_same_v<std::variant_alternative_t<i, VariantType>, T>) {
    return i;
  } else {
    return variant_index<VariantType, T, i + 1>();
  }
}

template <typename T, typename VariantType, size_t i = 0>
constexpr T& unchecked_get(VariantType& variant) {
  static_assert(i < std::variant_size_v<VariantType>,
                "Type not found in variant");
  if constexpr (std::is_same_v<std::variant_alternative_t<i, VariantType>, T>) {
    PERFETTO_DCHECK(std::holds_alternative<T>(variant));
    auto* v = std::get_if<T>(&variant);
    PERFETTO_ASSUME(v);
    return *v;
  } else {
    return unchecked_get<T, VariantType, i + 1>(variant);
  }
}

template <typename T, typename VariantType, size_t i = 0>
constexpr const T& unchecked_get(const VariantType& variant) {
  static_assert(i < std::variant_size_v<VariantType>,
                "Type not found in variant");
  if constexpr (std::is_same_v<std::variant_alternative_t<i, VariantType>, T>) {
    PERFETTO_DCHECK(std::holds_alternative<T>(variant));
    const auto* v = std::get_if<T>(&variant);
    PERFETTO_ASSUME(v != nullptr);
    return *v;
  } else {
    return unchecked_get<T, VariantType, i + 1>(variant);
  }
}

}  // namespace perfetto::base

#endif  // INCLUDE_PERFETTO_EXT_BASE_VARIANT_H_
