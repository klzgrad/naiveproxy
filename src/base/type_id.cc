// Copyright 2018 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "base/type_id.h"

#include "base/strings/string_number_conversions.h"

namespace base {
namespace experimental {

std::string TypeId::ToString() const {
#if DCHECK_IS_ON()
  return function_name_;
#else
  return NumberToString(reinterpret_cast<uintptr_t>(type_id_));
#endif
}

std::ostream& operator<<(std::ostream& out, const TypeId& type_id) {
  return out << type_id.ToString();
}

}  // namespace experimental
}  // namespace base
