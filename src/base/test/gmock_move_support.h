// Copyright 2019 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef BASE_TEST_GMOCK_MOVE_SUPPORT_H_
#define BASE_TEST_GMOCK_MOVE_SUPPORT_H_

#include "testing/gmock/include/gmock/gmock.h"

// A similar action as testing::SaveArg, but it does an assignment with
// std::move() instead of always performing a copy.
ACTION_TEMPLATE(MoveArg,
                HAS_1_TEMPLATE_PARAMS(int, k),
                AND_1_VALUE_PARAMS(out)) {
  *out = std::move(::testing::get<k>(args));
}

#endif  // BASE_TEST_GMOCK_MOVE_SUPPORT_H_
