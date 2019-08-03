// Copyright 2018 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef BASE_TEST_TYPE_ID_TEST_SUPPORT_A_H_
#define BASE_TEST_TYPE_ID_TEST_SUPPORT_A_H_

#include "base/component_export.h"
#include "base/type_id.h"

namespace base {

struct COMPONENT_EXPORT(BASE_TEST) TestType {};

// This is here to help test base::TypeId.
struct COMPONENT_EXPORT(BASE_TEST) TypeIdTestSupportA {
  static TypeId GetTypeIdForTypeInAnonymousNameSpace();
  static TypeId GetTypeIdForUniquePtrInt();
  static TypeId GetTypeIdForUniquePtrTestType();
};

}  // namespace base

#endif  // BASE_TEST_TYPE_ID_TEST_SUPPORT_A_H_
