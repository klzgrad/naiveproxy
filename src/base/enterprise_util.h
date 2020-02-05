// Copyright 2019 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef BASE_ENTERPRISE_UTIL_H_
#define BASE_ENTERPRISE_UTIL_H_

#include "base/base_export.h"

namespace base {

// Returns true if an outside entity manages the current machine. This includes
// but is not limited to the presence of user accounts from a centralized
// directory or the presence of dynamically updatable machine policies from an
// outside administrator.
BASE_EXPORT bool IsMachineExternallyManaged();

}  // namespace base

#endif  // BASE_ENTERPRISE_UTIL_H_
