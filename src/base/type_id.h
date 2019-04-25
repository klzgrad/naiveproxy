// Copyright 2018 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef BASE_TYPE_ID_H_
#define BASE_TYPE_ID_H_

#include <stdint.h>
#include <string>

#include "base/base_export.h"
#include "base/compiler_specific.h"
#include "base/logging.h"
#include "base/strings/string_piece.h"
#include "build/build_config.h"

#if defined(COMPILER_MSVC)
#include <typeindex>
#include <typeinfo>
#endif

namespace base {

namespace internal {

// The __attribute__((visibility("default"))) trick does not work for shared
// libraries in MSVC. MSVC does support a reduced functionality typeid operator
// with -fno-rtti, just enough for our needs.
// Biggest drawback is that using typeid prevents us from having constexpr
// methods (as the typeid operator is not constexpr)
#if defined(COMPILER_MSVC)
using TypeUniqueId = std::type_index;

template <typename Type>
inline TypeUniqueId UniqueIdFromType() {
  return std::type_index(typeid(Type));
}

#else
// A substitute for RTTI that uses the linker to uniquely reserve an address in
// the binary for each type.
// We need to make sure dummy_var has default visibility since we need to make
// sure that there is only one definition across all libraries (shared or
// static).
template <typename Type>
struct __attribute__((visibility("default"))) TypeTag {
  static constexpr char dummy_var = 0;
};

// static
template <typename Type>
constexpr char TypeTag<Type>::dummy_var;

using TypeUniqueId = const void*;

template <typename Type>
constexpr inline TypeUniqueId UniqueIdFromType() {
  return &TypeTag<Type>::dummy_var;
}
#endif

struct NoType {};

}  // namespace internal

class BASE_EXPORT TypeId {
 public:
  template <typename T>
  static TypeId From() {
    return TypeId(
#if DCHECK_IS_ON()
        PRETTY_FUNCTION,
#endif
        internal::UniqueIdFromType<T>());
  }

  TypeId();

  TypeId(const TypeId& other) = default;
  TypeId& operator=(const TypeId& other) = default;

  bool operator==(TypeId other) const {
    return unique_type_id_ == other.unique_type_id_;
  }

  bool operator!=(TypeId other) const { return !(*this == other); }

  std::string ToString() const;

 private:
  TypeId(
#if DCHECK_IS_ON()
      const char* function_name,
#endif
      internal::TypeUniqueId unique_type_id);

#if DCHECK_IS_ON()
  const char* function_name_;
#endif
  internal::TypeUniqueId unique_type_id_;
};

BASE_EXPORT std::ostream& operator<<(std::ostream& out, const TypeId& type_id);

}  // namespace base

#endif  // BASE_TYPE_ID_H_
