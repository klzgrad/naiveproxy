// Copyright 2018 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef BASE_TEST_TYPE_ID_TEST_SUPPORT_B_H_
#define BASE_TEST_TYPE_ID_TEST_SUPPORT_B_H_

#include "base/component_export.h"
#include "base/type_id.h"

namespace base {

// This is here to help test base::TypeId.
struct COMPONENT_EXPORT(BASE_TEST) TypeIdTestSupportB {
  static TypeId GetTypeIdForTypeInAnonymousNameSpace();
  static TypeId GetTypeIdForUniquePtrInt();
};

}  // namespace base

#endif  // BASE_TEST_TYPE_ID_TEST_SUPPORT_B_H_
