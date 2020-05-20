// Copyright 2019 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef QUICHE_COMMON_PLATFORM_API_QUICHE_ARRAYSIZE_H_
#define QUICHE_COMMON_PLATFORM_API_QUICHE_ARRAYSIZE_H_

#include "net/quiche/common/platform/impl/quiche_arraysize_impl.h"

#define QUICHE_ARRAYSIZE(array) QUICHE_ARRAYSIZE_IMPL(array)

#endif  // QUICHE_COMMON_PLATFORM_API_QUICHE_ARRAYSIZE_H_
