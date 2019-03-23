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

namespace base {
// Not ready for public consumption yet.
namespace experimental {

// A substitute for RTTI that uses the linker to uniquely reserve an address in
// the binary for each type.
class TypeId {
 public:
  bool constexpr operator==(const TypeId& other) const {
    return type_id_ == other.type_id_;
  }

  bool constexpr operator!=(const TypeId& other) const {
    return !(*this == other);
  }

 public:
  template <typename Type>
  static constexpr TypeId Create() {
    return TypeId(&TypeTag<Type>::dummy_var, PRETTY_FUNCTION);
  }

  std::string ToString() const;

 private:
  template <typename Type>
  struct TypeTag {
    constexpr static char dummy_var = 0;
  };

  constexpr TypeId(const void* type_id, const char* function_name)
      :
#if DCHECK_IS_ON()
        function_name_(function_name),
#endif
        type_id_(type_id) {
  }

#if DCHECK_IS_ON()
  const char* const function_name_;
#endif
  const void* const type_id_;
};

template <typename Type>
constexpr char TypeId::TypeTag<Type>::dummy_var;

BASE_EXPORT std::ostream& operator<<(std::ostream& out, const TypeId& type_id);

}  // namespace experimental
}  // namespace base

#endif  // BASE_TYPE_ID_H_
