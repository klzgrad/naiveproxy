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
#elif defined(COMPONENT_BUILD)
// A substitute for RTTI that uses the hash of the PRETTY_FUNCTION and
// __BASE_FILE__ to identify the type. Hash collisions can occur so don't use
// this in production code. The reason we use this at all is the dynamic linker
// can end up reserving two symbols for |dummy_var| below when the .SO is loaded
// first. This only seems to affect templates with non-builtin types, e.g.
// std::unique_ptr<int> is fine but |dummy_var| gets duplicated for
// std::unique_ptr<MyType>.
using TypeUniqueId = uint64_t;

// TODO(alexclarke): Replace these when StringPiece gets more constexpr support.
static constexpr bool string_starts_with(char const* s, char const* prefix) {
  while (*prefix) {
    if (*s++ != *prefix++)
      return false;
  }
  return true;
}

static constexpr bool string_contains(char const* str, char const* fragment) {
  while (*str) {
    if (string_starts_with(str++, fragment))
      return true;
  }
  return false;
}

template <typename Type>
constexpr inline TypeUniqueId UniqueIdFromType() {
  // This is an SDBM hash of PRETTY_FUNCTION, SMBD hash seems to be better than
  // djb2 or other simple hashes, and should be good enough for our purposes.
  // Source: http://www.cse.yorku.ca/~oz/hash.html
  constexpr const char* function_name = PRETTY_FUNCTION;
  uint64_t hash = 0;
  for (uint64_t i = 0; function_name[i]; ++i) {
    hash = function_name[i] + (hash << 6) + (hash << 16) - hash;
  }

  // There doesn't seem to be an official way of figuring out if a type is from
  // an anonymous namespace so we fall back to inspecting the decorated function
  // name.
  constexpr const char* compiler_specific_anonymous_namespace_fragment =
#if defined(__clang__)
      "base::internal::UniqueIdFromType() [Type = (anonymous namespace)::";
#elif defined(COMPILER_GCC)
      "base::internal::UniqueIdFromType() [with Type = {anonymous}::";
#elif defined(COMPILER_MSVC)
      "base::internal::UniqueIdFromType<`anonymous namespace'::";
#else
#error Compiler unsupported
#endif

  // To disambiguate types in anonymous namespaces add __BASE_FILE_ to the hash.
  if (string_contains(function_name,
                      compiler_specific_anonymous_namespace_fragment)) {
    constexpr auto* base_file = __BASE_FILE__;
    for (uint64_t i = 0; base_file[i]; ++i) {
      hash = base_file[i] + (hash << 6) + (hash << 16) - hash;
    }
  }
  return hash;
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
