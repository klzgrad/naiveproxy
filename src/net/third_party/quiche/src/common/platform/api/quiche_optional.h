// Copyright 2018 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef QUICHE_COMMON_PLATFORM_API_QUICHE_OPTIONAL_H_
#define QUICHE_COMMON_PLATFORM_API_QUICHE_OPTIONAL_H_

#include <utility>

#include "net/quiche/common/platform/impl/quiche_optional_impl.h"

namespace quiche {

template <typename T>
using QuicheOptional = QuicheOptionalImpl<T>;
#define QuicheNullOpt QuicheNullOptImpl

}  // namespace quiche

#endif  // QUICHE_COMMON_PLATFORM_API_QUICHE_OPTIONAL_H_
