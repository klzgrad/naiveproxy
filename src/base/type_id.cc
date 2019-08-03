// Copyright 2018 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "base/type_id.h"

#include "base/strings/string_number_conversions.h"

namespace base {

TypeId::TypeId(
#if DCHECK_IS_ON()
    const char* function_name,
#endif
    internal::TypeUniqueId unique_type_id)
    :
#if DCHECK_IS_ON()
      function_name_(function_name),
#endif
      unique_type_id_(unique_type_id) {
}

TypeId::TypeId()
    : TypeId(
#if DCHECK_IS_ON()
          "",
#endif
          internal::UniqueIdFromType<internal::NoType>()) {
}

std::string TypeId::ToString() const {
#if DCHECK_IS_ON()
  return function_name_;
#elif defined(COMPILER_MSVC)
  return HexEncode(&unique_type_id_, sizeof(unique_type_id_));
#elif defined(COMPONENT_BUILD)
  return NumberToString(unique_type_id_);
#else
  return NumberToString(reinterpret_cast<uintptr_t>(unique_type_id_));
#endif
}

std::ostream& operator<<(std::ostream& out, const TypeId& type_id) {
  return out << type_id.ToString();
}

}  // namespace base
